package klair

import vmem "core:mem/virtual"
import "core:testing"

@(test)
body_only_sends_given_settings :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	testing.expect_value(t, body_from_settings({on = false}), `{"lights":[{"on":0}]}`)
	testing.expect_value(t, body_from_settings({on = true}), `{"lights":[{"on":1}]}`)
	testing.expect_value(t, body_from_settings({on = true, brightness = 50}), `{"lights":[{"on":1,"brightness":50}]}`)
	testing.expect_value(t, body_from_settings({on = true, temperature = 213}), `{"lights":[{"on":1,"temperature":213}]}`)
	testing.expect_value(
		t,
		body_from_settings({on = true, brightness = 50, temperature = 213}),
		`{"lights":[{"on":1,"brightness":50,"temperature":213}]}`,
	)
}

@(test)
put_request_has_length_and_type :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	request := request_from_method(.Put, "127.0.0.1", {on = false})
	testing.expect_value(
		t,
		request,
		"PUT /elgato/lights HTTP/1.1\r\n" +
		"Host: 127.0.0.1\r\n" +
		"Content-Type: application/json\r\n" +
		"Accept: application/json\r\n" +
		"Content-Length: 21\r\n" +
		"Connection: close\r\n" +
		"\r\n" +
		`{"lights":[{"on":0}]}`,
	)
}

@(test)
response_with_length_waits_for_the_body :: proc(t: ^testing.T) {
	HEAD :: "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"

	_, check := check_response(HEAD + "ab", closed = false)
	testing.expect_value(t, check, Response_Check.Incomplete)

	response: Response
	response, check = check_response(HEAD + "abcde", closed = false)
	testing.expect_value(t, check, Response_Check.Complete)
	testing.expect_value(t, response.status, 200)
	testing.expect_value(t, response.body, "abcde")

	_, check = check_response(HEAD + "ab", closed = true)
	testing.expect_value(t, check, Response_Check.Closed_Early)
}

@(test)
response_without_length_completes_on_close :: proc(t: ^testing.T) {
	data := "HTTP/1.0 500 Internal Server Error\r\n\r\noops"

	_, check := check_response(data, closed = false)
	testing.expect_value(t, check, Response_Check.Incomplete)

	response: Response
	response, check = check_response(data, closed = true)
	testing.expect_value(t, check, Response_Check.Complete)
	testing.expect_value(t, response.status, 500)
	testing.expect_value(t, response.body, "oops")
}

@(test)
response_edge_cases :: proc(t: ^testing.T) {
	_, check := check_response("HTTP/1.1 200 OK\r\n", closed = true)
	testing.expect_value(t, check, Response_Check.Closed_Early)

	_, check = check_response("garbage\r\n\r\n", closed = true)
	testing.expect_value(t, check, Response_Check.Malformed)

	_, check = check_response("HTTP/1.1 200 OK\r\nContent-Length: x\r\n\r\n", closed = false)
	testing.expect_value(t, check, Response_Check.Malformed)

	_, check = check_response("HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n", closed = false)
	testing.expect_value(t, check, Response_Check.Complete)
}

@(test)
light_state_needs_all_three_integers :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	state, ok := light_state_from_body(`{"numberOfLights":1,"lights":[{"on":1,"brightness":20,"temperature":213}]}`)
	testing.expect(t, ok)
	testing.expect_value(t, state, Light_State{on = 1, brightness = 20, temperature = 213})

	bad := [?]string {
		"not json",
		`{}`,
		`{"lights":[]}`,
		`{"lights":{}}`,
		`{"lights":[{"on":1,"brightness":20}]}`, // temperature missing
		`{"lights":[{"on":1,"brightness":20,"temperature":0}]}`,
		`{"lights":[{"on":1,"brightness":20,"temperature":-5}]}`,
		`{"lights":[{"on":1,"brightness":20.5,"temperature":213}]}`,
		`{"lights":[{"on":"1","brightness":20,"temperature":213}]}`,
	}
	for body in bad {
		_, ok = light_state_from_body(body)
		testing.expect(t, !ok, body)
	}
}
