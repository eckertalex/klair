package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"
)

type FakeLight struct {
	Light    Light
	mutex    sync.Mutex
	requests []string
	problems []string // requests that break the spec's request rules
}

type Behavior int

const (
	BEHAVIOR_NORMAL    Behavior = iota // Content-Length, state reflects PUTs
	BEHAVIOR_NO_LENGTH                 // no Content-Length, then closes
	BEHAVIOR_FLAKY                     // drops the first connection
	BEHAVIOR_ERROR_500
	BEHAVIOR_GARBAGE // 200 with "not json"
	BEHAVIOR_HANGUP  // Content-Length 1000, half a body, closes
	BEHAVIOR_HUGE    // a valid body padded past MAX_RESPONSE
	BEHAVIOR_REDIRECT
	BEHAVIOR_SILENT // never answers
)

func startFakeLight(t *testing.T, name string, behavior Behavior) *FakeLight {
	light := &FakeLight{}
	state := LightState{On: 1, Brightness: 20, Temperature: 213}
	connections := 0

	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body, _ := io.ReadAll(r.Body)

		light.mutex.Lock()
		defer light.mutex.Unlock()
		connections++

		if behavior == BEHAVIOR_FLAKY && connections == 1 {
			light.requests = append(light.requests, "dropped")
			hijackAndWrite(w, "")
			return
		}

		light.requests = append(light.requests, strings.TrimSpace(r.Method+" "+string(body)))
		if !r.Close || r.Host != light.Light.Host || r.URL.Path != "/elgato/lights" {
			light.problems = append(light.problems, r.Method+" "+r.Host+" "+r.URL.Path)
		}
		if r.Method == http.MethodPut && r.Header.Get("Content-Type") != "application/json" {
			light.problems = append(light.problems, "PUT without Content-Type")
		}
		if behavior == BEHAVIOR_REDIRECT {
			http.Redirect(w, r, "http://example.invalid/", http.StatusFound)
			return
		}

		if r.Method == http.MethodPut {
			var update struct {
				Lights []struct {
					On          *int
					Brightness  *int
					Temperature *int
				}
			}
			if json.Unmarshal(body, &update) == nil && len(update.Lights) > 0 {
				if v := update.Lights[0].On; v != nil {
					state.On = *v
				}
				if v := update.Lights[0].Brightness; v != nil {
					state.Brightness = *v
				}
				if v := update.Lights[0].Temperature; v != nil {
					state.Temperature = *v
				}
			}
		}
		answer := string(bodyFromState(state))

		switch behavior {
		case BEHAVIOR_NORMAL, BEHAVIOR_FLAKY:
			w.Header().Set("Content-Type", "application/json")
			io.WriteString(w, answer)
		case BEHAVIOR_NO_LENGTH:
			hijackAndWrite(w, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"+answer)
		case BEHAVIOR_ERROR_500:
			w.WriteHeader(http.StatusInternalServerError)
		case BEHAVIOR_GARBAGE:
			io.WriteString(w, "not json")
		case BEHAVIOR_HANGUP:
			hijackAndWrite(w, "HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n"+answer[:len(answer)/2])
		case BEHAVIOR_HUGE:
			io.WriteString(w, answer+strings.Repeat(" ", MAX_RESPONSE))
		case BEHAVIOR_SILENT:
			<-r.Context().Done()
		}
	}))
	t.Cleanup(server.Close)

	address := strings.TrimPrefix(server.URL, "http://")
	light.Light = Light{Name: name, Address: address, Host: "127.0.0.1"}
	return light
}

func hijackAndWrite(w http.ResponseWriter, raw string) {
	conn, _, err := http.NewResponseController(w).Hijack()
	assert(err == nil, "httptest connections can be hijacked")
	io.WriteString(conn, raw)
	conn.Close()
}

func bodyFromState(state LightState) []byte {
	return fmt.Appendf(nil, `{"numberOfLights":1,"lights":[{"on":%d,"brightness":%d,"temperature":%d}]}`,
		state.On, state.Brightness, state.Temperature)
}

func requestsOf(light *FakeLight) []string {
	light.mutex.Lock()
	defer light.mutex.Unlock()
	return append([]string(nil), light.requests...)
}

func problemsOf(light *FakeLight) []string {
	light.mutex.Lock()
	defer light.mutex.Unlock()
	return append([]string(nil), light.problems...)
}

func refusedLight(t *testing.T, name string) Light {
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	address := listener.Addr().String()
	listener.Close()
	return Light{Name: name, Address: address, Host: "127.0.0.1"}
}

func TestRequestResults(t *testing.T) {
	tests := []struct {
		behavior Behavior
		want     Result
	}{
		{BEHAVIOR_NORMAL, Result{State: LightState{On: 1, Brightness: 20, Temperature: 213}}},
		{BEHAVIOR_NO_LENGTH, Result{State: LightState{On: 1, Brightness: 20, Temperature: 213}}},
		{BEHAVIOR_ERROR_500, Result{Error: ERROR_STATUS, Status: 500}},
		{BEHAVIOR_GARBAGE, Result{Error: ERROR_INVALID}},
		{BEHAVIOR_HANGUP, Result{Error: ERROR_CLOSED}},
		{BEHAVIOR_HUGE, Result{Error: ERROR_INVALID}},
		{BEHAVIOR_REDIRECT, Result{Error: ERROR_STATUS, Status: http.StatusFound}},
	}

	client := makeClient()
	for _, tt := range tests {
		light := startFakeLight(t, "light", tt.behavior)
		if got := request(client, light.Light, nil); got != tt.want {
			t.Errorf("behavior %d: request = %+v, want %+v", tt.behavior, got, tt.want)
		}
		if problems := problemsOf(light); len(problems) != 0 {
			t.Errorf("behavior %d: requests broke the spec: %v", tt.behavior, problems)
		}
	}

	if got := request(client, refusedLight(t, "refused"), nil); got.Error != ERROR_CONNECT {
		t.Errorf("refused: request = %+v, want ERROR_CONNECT", got)
	}
}

func TestRequestTimesOut(t *testing.T) {
	light := startFakeLight(t, "silent", BEHAVIOR_SILENT)
	client := makeClient()
	client.Timeout = 100 * time.Millisecond

	if got := request(client, light.Light, nil); got.Error != ERROR_TIMEOUT {
		t.Errorf("request = %+v, want ERROR_TIMEOUT", got)
	}
}

func TestRetryBacksOffThenGivesUp(t *testing.T) {
	retry := Retry{attemptsLeft: MAX_ATTEMPTS, backoff: FIRST_BACKOFF}
	for _, want := range []time.Duration{100 * time.Millisecond, 200 * time.Millisecond} {
		wait, again := retryAdvance(&retry)
		if !again || wait != want {
			t.Errorf("retryAdvance = %v, %v, want %v, true", wait, again, want)
		}
	}
	if _, again := retryAdvance(&retry); again {
		t.Error("retryAdvance wants a fourth attempt")
	}
}

func TestRetriesAfterADroppedConnection(t *testing.T) {
	light := startFakeLight(t, "flaky", BEHAVIOR_FLAKY)

	result := requestWithRetries(makeClient(), light.Light, nil)
	if result.Error != ERROR_NONE {
		t.Errorf("requestWithRetries = %+v", result)
	}
	requests := requestsOf(light)
	if len(requests) != 2 || requests[0] != "dropped" || requests[1] != "GET" {
		t.Errorf("requests = %q, want dropped then GET", requests)
	}
}

func TestGivesUpAfterThreeAttempts(t *testing.T) {
	light := startFakeLight(t, "error500", BEHAVIOR_ERROR_500)

	start := time.Now()
	result := requestWithRetries(makeClient(), light.Light, nil)
	elapsed := time.Since(start)

	if result.Error != ERROR_STATUS || result.Status != 500 {
		t.Errorf("requestWithRetries = %+v", result)
	}
	if n := len(requestsOf(light)); n != 3 {
		t.Errorf("%d requests, want 3", n)
	}
	if elapsed < 300*time.Millisecond || elapsed > 600*time.Millisecond {
		t.Errorf("took %v, want about 300ms", elapsed)
	}
}

func TestAllLightsAtOnce(t *testing.T) {
	lights := []Light{
		startFakeLight(t, "a", BEHAVIOR_ERROR_500).Light,
		startFakeLight(t, "b", BEHAVIOR_ERROR_500).Light,
		startFakeLight(t, "c", BEHAVIOR_NORMAL).Light,
	}

	start := time.Now()
	results := requestAll(lights, nil)
	elapsed := time.Since(start)

	if results[0].Error != ERROR_STATUS || results[1].Error != ERROR_STATUS || results[2].Error != ERROR_NONE {
		t.Errorf("results = %+v, not in config order", results)
	}
	if elapsed > 500*time.Millisecond {
		t.Errorf("took %v, the lights waited on each other", elapsed)
	}
}
