package main

import "encoding/json"

type LightState struct {
	On          int
	Brightness  int
	Temperature int // mired
}

type Settings struct {
	On          int  `json:"on"`
	Brightness  *int `json:"brightness,omitempty"`
	Temperature *int `json:"temperature,omitempty"` // mired
}

func bodyFromSettings(settings Settings) []byte {
	body, err := json.Marshal(struct {
		Lights []Settings `json:"lights"`
	}{Lights: []Settings{settings}})
	assert(err == nil, "a struct of ints always marshals")
	return body
}

func lightStateFromBody(body []byte) (LightState, bool) {
	var parsed struct {
		Lights []struct {
			On          *int `json:"on"`
			Brightness  *int `json:"brightness"`
			Temperature *int `json:"temperature"`
		} `json:"lights"`
	}
	if json.Unmarshal(body, &parsed) != nil || len(parsed.Lights) == 0 {
		return LightState{}, false
	}

	light := parsed.Lights[0]
	if light.On == nil || light.Brightness == nil || light.Temperature == nil {
		return LightState{}, false
	}
	if *light.Temperature <= 0 {
		return LightState{}, false
	}

	return LightState{On: *light.On, Brightness: *light.Brightness, Temperature: *light.Temperature}, true
}
