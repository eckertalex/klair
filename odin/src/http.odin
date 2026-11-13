package klair

import "core:encoding/json"
import "core:fmt"
import "core:strconv"
import "core:strings"

Method :: enum {
	Get,
	Put,
}

Light_State :: struct {
	on:          int,
	brightness:  int,
	temperature: int, // mired
}

Settings :: struct {
	on:          bool,
	brightness:  Maybe(int),
	temperature: Maybe(int), // mired
}

Response :: struct {
	status: int,
	body:   string,
}

Response_Check :: enum {
	Incomplete, // keep reading
	Complete,
	Closed_Early, // the light hung up before the response was complete
	Malformed,
}

body_from_settings :: proc(settings: Settings) -> string {
	b := strings.builder_make()
	fmt.sbprintf(&b, `{{"lights":[{{"on":%d`, 1 if settings.on else 0)
	if brightness, ok := settings.brightness.?; ok {
		fmt.sbprintf(&b, `,"brightness":%d`, brightness)
	}
	if temperature, ok := settings.temperature.?; ok {
		fmt.sbprintf(&b, `,"temperature":%d`, temperature)
	}
	strings.write_string(&b, "}]}")
	return strings.to_string(b)
}

request_from_method :: proc(method: Method, host: string, settings: Settings) -> string {
	switch method {
	case .Get:
		return fmt.aprintf(
			"GET /elgato/lights HTTP/1.1\r\n" +
			"Host: %s\r\n" +
			"Accept: application/json\r\n" +
			"Connection: close\r\n" +
			"\r\n",
			host,
		)
	case .Put:
		body := body_from_settings(settings)
		return fmt.aprintf(
			"PUT /elgato/lights HTTP/1.1\r\n" +
			"Host: %s\r\n" +
			"Content-Type: application/json\r\n" +
			"Accept: application/json\r\n" +
			"Content-Length: %d\r\n" +
			"Connection: close\r\n" +
			"\r\n" +
			"%s",
			host,
			len(body),
			body,
		)
	}
	unreachable()
}

// Complete at Content-Length, or on close when it is absent.
check_response :: proc(data: string, closed: bool) -> (response: Response, check: Response_Check) {
	header_end := strings.index(data, "\r\n\r\n")
	if header_end < 0 {
		return {}, .Closed_Early if closed else .Incomplete
	}
	headers := data[:header_end]
	body := data[header_end + len("\r\n\r\n"):]

	status_line, _, header_lines := strings.partition(headers, "\r\n")
	version, _, after_version := strings.partition(status_line, " ")
	status_text, _, _ := strings.partition(after_version, " ")
	if !strings.has_prefix(version, "HTTP/") || len(status_text) != 3 {
		return {}, .Malformed
	}
	status, status_ok := strconv.parse_int(status_text, 10)
	if !status_ok {
		return {}, .Malformed
	}

	length: Maybe(int)
	for line in strings.split_iterator(&header_lines, "\r\n") {
		name, _, value := strings.partition(line, ":")
		if strings.equal_fold(strings.trim_space(name), "content-length") {
			value_length, length_ok := strconv.parse_int(strings.trim_space(value), 10)
			if !length_ok {
				return {}, .Malformed
			}
			length = value_length
		}
	}

	if expected, has_length := length.?; has_length {
		if expected < 0 {
			return {}, .Malformed
		}
		if len(body) >= expected {
			return {status, body[:expected]}, .Complete
		}
		return {}, .Closed_Early if closed else .Incomplete
	}
	if closed {
		return {status, body}, .Complete
	}
	return {}, .Incomplete
}

light_state_from_body :: proc(body: string) -> (state: Light_State, ok: bool) {
	value, err := json.parse_string(body, parse_integers = true)
	if err != .None {
		return
	}
	root := value.(json.Object) or_return
	lights := root["lights"].(json.Array) or_return
	if len(lights) == 0 {
		return
	}
	light := lights[0].(json.Object) or_return

	on := light["on"].(json.Integer) or_return
	brightness := light["brightness"].(json.Integer) or_return
	temperature := light["temperature"].(json.Integer) or_return
	if temperature <= 0 {
		return
	}
	return {int(on), int(brightness), int(temperature)}, true
}
