package conformance

import "core:fmt"
import "core:testing"

// Host and Connection: close, which every request needs
HEADERS :: "Host: 127.0.0.1\r\nConnection: close\r\n"
GET :: "GET /elgato/lights HTTP/1.1\r\n" + HEADERS + "\r\n"
BODY :: `{"lights":[{"on":1,"brightness":50,"temperature":213}]}`

put :: proc(body: string) -> string {
	return build_put("Content-Type: application/json", body)
}

// A PUT with HEADERS, the given ones and a matching Content-Length
build_put :: proc(headers, body: string) -> string {
	return fmt.tprintf(
		"PUT /elgato/lights HTTP/1.1\r\n%s%s\r\nContent-Length: %d\r\n\r\n%s",
		HEADERS,
		headers,
		len(body),
		body,
	)
}

// Parses a complete request and returns what check_request says about it
violation_of :: proc(t: ^testing.T, data: string, loc := #caller_location) -> string {
	request, status, problem := parse_request(data)
	testing.expect_value(t, status, Parse_Status.Complete, loc)
	testing.expect_value(t, problem, "", loc)
	return check_request(&request)
}

@(test)
request_parses_line_headers_and_body :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	request, status, _ := parse_request(put(BODY))
	testing.expect_value(t, status, Parse_Status.Complete)
	testing.expect_value(t, request.method, "PUT")
	testing.expect_value(t, request.path, "/elgato/lights")
	testing.expect_value(t, request.version, "HTTP/1.1")
	testing.expect_value(t, request.content_length, len(BODY))
	testing.expect_value(t, request.body, BODY)
	host, has_host := find_header(&request, "host")
	testing.expect(t, has_host)
	testing.expect_value(t, host, "127.0.0.1")
}

@(test)
request_waits_for_headers_and_body :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	full := put(BODY)
	_, status, _ := parse_request(full[:20])
	testing.expect_value(t, status, Parse_Status.Incomplete)
	request: Request
	request, status, _ = parse_request(full[:len(full) - 5])
	testing.expect_value(t, status, Parse_Status.Incomplete)
	testing.expect_value(t, request.content_length, len(BODY))
	_, status, _ = parse_request(GET)
	testing.expect_value(t, status, Parse_Status.Complete)
}

@(test)
request_rejects_malformed_input :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	_, status, problem := parse_request("GET\r\n\r\n")
	testing.expect_value(t, status, Parse_Status.Malformed)
	testing.expect_value(t, problem, `malformed request line "GET"`)

	_, status, problem = parse_request("GET /elgato/lights HTTP/1.1\r\nno colon\r\n\r\n")
	testing.expect_value(t, status, Parse_Status.Malformed)
	testing.expect_value(t, problem, `malformed header "no colon"`)

	_, status, problem = parse_request("PUT /elgato/lights HTTP/1.1\r\nContent-Length: +5\r\n\r\n")
	testing.expect_value(t, status, Parse_Status.Malformed)
	testing.expect_value(t, problem, `invalid Content-Length "+5"`)

	_, status, problem = parse_request(GET + "extra")
	testing.expect_value(t, status, Parse_Status.Malformed)
	testing.expect_value(t, problem, "more bytes than Content-Length")
}

@(test)
check_accepts_what_the_spec_allows :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	testing.expect_value(t, violation_of(t, GET), "")
	testing.expect_value(t, violation_of(t, put(BODY)), "")
	// Header names and the close value in any case, extra headers allowed
	testing.expect_value(
		t,
		violation_of(t, "GET /elgato/lights HTTP/1.1\r\nhost: x\r\nCONNECTION: Close\r\nUser-Agent: Go\r\n\r\n"),
		"",
	)
}

@(test)
check_rejects_each_rule :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	testing.expect_value(t, violation_of(t, "POST /elgato/lights HTTP/1.1\r\n" + HEADERS + "\r\n"), "method POST")
	testing.expect_value(t, violation_of(t, "GET /elgato/light HTTP/1.1\r\n" + HEADERS + "\r\n"), "path /elgato/light")
	testing.expect_value(t, violation_of(t, "GET /elgato/lights HTTP/1.0\r\n" + HEADERS + "\r\n"), "version HTTP/1.0")
	testing.expect_value(t, violation_of(t, "GET /elgato/lights HTTP/2\r\n" + HEADERS + "\r\n"), "version HTTP/2")
	testing.expect_value(
		t,
		violation_of(t, "GET /elgato/lights HTTP/1.1\r\nHost: x\r\n\r\n"),
		"no Connection: close",
	)
	testing.expect_value(
		t,
		violation_of(t, "GET /elgato/lights HTTP/1.1\r\nConnection: keep-alive\r\nHost: x\r\n\r\n"),
		"no Connection: close",
	)
	testing.expect_value(
		t,
		violation_of(t, "GET /elgato/lights HTTP/1.1\r\nConnection: close\r\n\r\n"),
		"no Host",
	)
	testing.expect_value(
		t,
		violation_of(t, "GET /elgato/lights HTTP/1.1\r\n" + HEADERS + "Content-Length: 2\r\n\r\n{}"),
		"GET with a body",
	)
	testing.expect_value(t, violation_of(t, build_put("X: y", BODY)), "PUT without Content-Type")
	testing.expect_value(t, violation_of(t, build_put("Content-Type: text/plain", BODY)), "Content-Type text/plain")
	testing.expect_value(
		t,
		violation_of(t, "PUT /elgato/lights HTTP/1.1\r\n" + HEADERS + "Content-Type: application/json\r\n\r\n"),
		"PUT without Content-Length",
	)
	testing.expect_value(t, violation_of(t, put(`{"lights":[{"on":2}]}`)), `unexpected body {"lights":[{"on":2}]}`)
}

@(test)
body_takes_only_the_spec_shape :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	update, violation := parse_body(`{"lights":[{"on":0}]}`)
	testing.expect_value(t, violation, "")
	testing.expect_value(t, update.on, 0)
	testing.expect(t, update.brightness == nil && update.temperature == nil)

	update, violation = parse_body(`{"lights":[{"on":1,"temperature":344}]}`)
	testing.expect_value(t, violation, "")
	testing.expect(t, update.brightness == nil)
	testing.expect_value(t, update.temperature.?, 344)

	update, violation = parse_body(BODY)
	testing.expect_value(t, violation, "")
	testing.expect_value(t, update.brightness.?, 50)
	testing.expect_value(t, update.temperature.?, 213)

	rejected := []string {
		`{"lights":[{"brightness":50}]}`,                    // no on
		`{"lights":[{"on":true}]}`,                          // on not a number
		`{"lights":[{"on":01}]}`,                            // leading zero
		`{"lights":[{"on":1,"brightness":-5}]}`,             // sign
		`{"lights":[{"on":1,"temperature":213,"brightness":50}]}`, // order
		`{"lights":[{"on":1, "brightness":50}]}`,            // whitespace
		`{"lights": [{"on":1}]}`,                            // whitespace
		`{"lights":[{"on":1}]} `,                            // trailing
		`{"lights":[{"on":1,"brightness":50,"brightness":60}]}`, // repeated
		`{"lights":[{"on":1}],"numberOfLights":1}`,          // extra key
		``,
	}
	for body in rejected {
		_, violation = parse_body(body)
		testing.expectf(t, violation == fmt.tprintf("unexpected body %s", body), "%s: %s", body, violation)
	}
}

@(test)
body_ranges :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	_, violation := parse_body(`{"lights":[{"on":1,"brightness":3,"temperature":143}]}`)
	testing.expect_value(t, violation, "")
	_, violation = parse_body(`{"lights":[{"on":1,"brightness":100,"temperature":344}]}`)
	testing.expect_value(t, violation, "")
	_, violation = parse_body(`{"lights":[{"on":1,"brightness":2}]}`)
	testing.expect_value(t, violation, "brightness 2 out of range")
	_, violation = parse_body(`{"lights":[{"on":1,"brightness":101}]}`)
	testing.expect_value(t, violation, "brightness 101 out of range")
	_, violation = parse_body(`{"lights":[{"on":1,"temperature":142}]}`)
	testing.expect_value(t, violation, "temperature 142 out of range")
	_, violation = parse_body(`{"lights":[{"on":1,"temperature":345}]}`)
	testing.expect_value(t, violation, "temperature 345 out of range")
}

@(test)
update_changes_only_sent_fields :: proc(t: ^testing.T) {
	state := DEFAULT_STATE
	apply_update(&state, {on = 0})
	testing.expect_value(t, state, Light_State{on = 0, brightness = 20, temperature = 213})
	apply_update(&state, {on = 1, brightness = 50})
	testing.expect_value(t, state, Light_State{on = 1, brightness = 50, temperature = 213})
	apply_update(&state, {on = 1, temperature = 344})
	testing.expect_value(t, state, Light_State{on = 1, brightness = 50, temperature = 344})
}

@(test)
response_with_and_without_length :: proc(t: ^testing.T) {
	defer free_all(context.temp_allocator)

	body := json_from_state(DEFAULT_STATE)
	testing.expect_value(t, body, `{"numberOfLights":1,"lights":[{"on":1,"brightness":20,"temperature":213}]}`)
	testing.expect_value(
		t,
		build_response("200 OK", body, true),
		"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 74\r\nConnection: close\r\n\r\n" + `{"numberOfLights":1,"lights":[{"on":1,"brightness":20,"temperature":213}]}`,
	)
	testing.expect_value(
		t,
		build_response("500 Internal Server Error", "", false),
		"HTTP/1.1 500 Internal Server Error\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n",
	)
}
