package main

import "testing"

func TestBodyOnlySendsGivenSettings(t *testing.T) {
	brightness, mired := 50, 213
	tests := []struct {
		settings Settings
		want     string
	}{
		{Settings{On: 0}, `{"lights":[{"on":0}]}`},
		{Settings{On: 1}, `{"lights":[{"on":1}]}`},
		{Settings{On: 1, Brightness: &brightness}, `{"lights":[{"on":1,"brightness":50}]}`},
		{Settings{On: 1, Temperature: &mired}, `{"lights":[{"on":1,"temperature":213}]}`},
		{Settings{On: 1, Brightness: &brightness, Temperature: &mired}, `{"lights":[{"on":1,"brightness":50,"temperature":213}]}`},
	}

	for _, tt := range tests {
		if got := string(bodyFromSettings(tt.settings)); got != tt.want {
			t.Errorf("body = %s, want %s", got, tt.want)
		}
	}
}

func TestLightStateFromBody(t *testing.T) {
	state, ok := lightStateFromBody([]byte(`{"numberOfLights":1,"lights":[{"on":1,"brightness":20,"temperature":213}]}`))
	if !ok || state != (LightState{On: 1, Brightness: 20, Temperature: 213}) {
		t.Errorf("lightStateFromBody = %+v, %v", state, ok)
	}

	state, ok = lightStateFromBody([]byte(`{"lights":[{"on":0,"brightness":3,"temperature":344},{"on":1}]}`))
	if !ok || state != (LightState{On: 0, Brightness: 3, Temperature: 344}) {
		t.Errorf("lightStateFromBody = %+v, %v", state, ok)
	}

	bad := []string{
		"not json",
		"",
		`{}`,
		`{"lights":[]}`,
		`{"lights":[{"on":1,"brightness":20}]}`,
		`{"lights":[{"on":1,"temperature":213}]}`,
		`{"lights":[{"brightness":20,"temperature":213}]}`,
		`{"lights":[{"on":1,"brightness":20,"temperature":0}]}`,
		`{"lights":[{"on":1,"brightness":20,"temperature":-5}]}`,
		`{"lights":[{"on":true,"brightness":20,"temperature":213}]}`,
		`{"lights":[{"on":1,"brightness":20.5,"temperature":213}]}`,
		`{"lights":[{"on":1,"brightness":"20","temperature":213}]}`,
	}
	for _, body := range bad {
		if state, ok := lightStateFromBody([]byte(body)); ok {
			t.Errorf("lightStateFromBody(%s) accepted it: %+v", body, state)
		}
	}
}
