package main

import (
	"bytes"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"sync"
	"time"
)

const (
	ATTEMPT_TIMEOUT = 3 * time.Second
	MAX_ATTEMPTS    = 3
	FIRST_BACKOFF   = 100 * time.Millisecond
	MAX_RESPONSE    = 64 << 10
)

type ErrorKind int

const (
	ERROR_NONE ErrorKind = iota
	ERROR_CONNECT
	ERROR_TIMEOUT
	ERROR_CLOSED
	ERROR_STATUS
	ERROR_INVALID
)

type Result struct {
	State  LightState
	Error  ErrorKind
	Status int
}

func messageFromResult(result Result) string {
	switch result.Error {
	case ERROR_CONNECT:
		return "failed to connect"
	case ERROR_TIMEOUT:
		return "timed out"
	case ERROR_CLOSED:
		return "connection closed unexpectedly"
	case ERROR_STATUS:
		return fmt.Sprintf("unexpected status %d", result.Status)
	case ERROR_INVALID:
		return "invalid response"
	}
	assert(false, "no message for ERROR_NONE")
	return ""
}

func errorKindFromHTTP(err error) ErrorKind {
	var netErr net.Error
	if errors.As(err, &netErr) && netErr.Timeout() {
		return ERROR_TIMEOUT
	}
	if errors.Is(err, io.EOF) || errors.Is(err, io.ErrUnexpectedEOF) {
		return ERROR_CLOSED
	}
	return ERROR_CONNECT
}

func makeClient() *http.Client {
	return &http.Client{
		Timeout: ATTEMPT_TIMEOUT,
		CheckRedirect: func(_ *http.Request, _ []*http.Request) error {
			return http.ErrUseLastResponse
		},
		Transport: &http.Transport{
			Proxy:                  nil,
			DisableKeepAlives:      true,
			DisableCompression:     true,
			MaxResponseHeaderBytes: MAX_RESPONSE,
		},
	}
}

func request(client *http.Client, light Light, settings *Settings) Result {
	url := "http://" + light.Address + "/elgato/lights"

	var req *http.Request
	var err error
	if settings == nil {
		req, err = http.NewRequest(http.MethodGet, url, nil)
	} else {
		req, err = http.NewRequest(http.MethodPut, url, bytes.NewReader(bodyFromSettings(*settings)))
	}
	if err != nil {
		return Result{Error: ERROR_CONNECT}
	}
	if settings != nil {
		req.Header.Set("Content-Type", "application/json")
	}
	req.Header.Set("Accept", "application/json")
	req.Host = light.Host
	req.Close = true

	resp, err := client.Do(req)
	if err != nil {
		return Result{Error: errorKindFromHTTP(err)}
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(io.LimitReader(resp.Body, MAX_RESPONSE+1))
	if err != nil {
		return Result{Error: errorKindFromHTTP(err)}
	}

	if len(body) > MAX_RESPONSE {
		return Result{Error: ERROR_INVALID}
	}
	if resp.StatusCode != http.StatusOK {
		return Result{Error: ERROR_STATUS, Status: resp.StatusCode}
	}

	state, ok := lightStateFromBody(body)
	if !ok {
		return Result{Error: ERROR_INVALID}
	}
	return Result{State: state}
}

type Retry struct {
	attemptsLeft int
	backoff      time.Duration
}

func retryAdvance(retry *Retry) (wait time.Duration, again bool) {
	assert(retry.attemptsLeft > 0, "retryAdvance called after giving up")
	retry.attemptsLeft--
	if retry.attemptsLeft == 0 {
		return 0, false
	}

	wait = retry.backoff
	retry.backoff *= 2
	return wait, true
}

func requestWithRetries(client *http.Client, light Light, settings *Settings) Result {
	retry := Retry{attemptsLeft: MAX_ATTEMPTS, backoff: FIRST_BACKOFF}
	for {
		result := request(client, light, settings)
		if result.Error == ERROR_NONE {
			return result
		}

		wait, again := retryAdvance(&retry)
		if !again {
			return result
		}
		time.Sleep(wait)
	}
}

func requestAll(lights []Light, settings *Settings) []Result {
	client := makeClient()
	results := make([]Result, len(lights))

	var wg sync.WaitGroup
	for i, light := range lights {
		wg.Go(func() {
			results[i] = requestWithRetries(client, light, settings)
		})
	}
	wg.Wait()

	return results
}
