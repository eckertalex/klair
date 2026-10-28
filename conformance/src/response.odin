package conformance

import "core:fmt"
import "core:strings"

Light_State :: struct {
	on:          int,
	brightness:  int,
	temperature: int, // mired
}

DEFAULT_STATE :: Light_State{on = 1, brightness = 20, temperature = 213}

// Only the fields the PUT sent change, like on a real light
apply_update :: proc(state: ^Light_State, update: Light_Update) {
	state.on = update.on
	if brightness, ok := update.brightness.?; ok {
		state.brightness = brightness
	}
	if temperature, ok := update.temperature.?; ok {
		state.temperature = temperature
	}
}

// What a real Key Light Air answers to GET and PUT
json_from_state :: proc(state: Light_State) -> string {
	return fmt.tprintf(
		`{{"numberOfLights":1,"lights":[{{"on":%d,"brightness":%d,"temperature":%d}}]}}`,
		state.on,
		state.brightness,
		state.temperature,
	)
}

// `status` is the whole status line after the version, "200 OK"
build_response :: proc(status: string, body: string, with_length: bool) -> string {
	b := strings.builder_make(context.temp_allocator)
	fmt.sbprintf(&b, "HTTP/1.1 %s\r\n", status)
	strings.write_string(&b, "Content-Type: application/json\r\n")
	if with_length {
		fmt.sbprintf(&b, "Content-Length: %d\r\n", len(body))
	}
	strings.write_string(&b, "Connection: close\r\n\r\n")
	strings.write_string(&b, body)
	return strings.to_string(b)
}
