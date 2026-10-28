package conformance

import "core:fmt"
import "core:strings"

// Parsing and checking requests. Nothing here touches a socket, so all of it
// is unit tested.

MAX_HEADERS :: 32

Header :: struct {
	name:  string,
	value: string,
}

Request :: struct {
	method:         string,
	path:           string,
	version:        string,
	headers:        [MAX_HEADERS]Header,
	header_count:   int,
	content_length: int, // -1 when the request has no Content-Length
	body:           string,
}

Parse_Status :: enum {
	Incomplete, // need more bytes
	Complete,
	Malformed,
}

// What a PUT asks for. Fields the body doesn't have stay nil.
Light_Update :: struct {
	on:          int,
	brightness:  Maybe(int),
	temperature: Maybe(int),
}

// Splits `data` into a request. Complete once the headers and Content-Length
// bytes of body are in. The strings are views into `data`.
parse_request :: proc(data: string) -> (request: Request, status: Parse_Status, problem: string) {
	request.content_length = -1

	header_end := strings.index(data, "\r\n\r\n")
	if header_end < 0 {
		return request, .Incomplete, ""
	}
	head := data[:header_end]
	request.body = data[header_end + len("\r\n\r\n"):]

	request_line, _, header_lines := strings.partition(head, "\r\n")
	rest: string
	request.method, _, rest = strings.partition(request_line, " ")
	request.path, _, request.version = strings.partition(rest, " ")
	if request.method == "" || request.path == "" || request.version == "" {
		return request, .Malformed, fmt.tprintf("malformed request line %q", request_line)
	}

	for line in strings.split_iterator(&header_lines, "\r\n") {
		name, colon, value := strings.partition(line, ":")
		if colon == "" {
			return request, .Malformed, fmt.tprintf("malformed header %q", line)
		}
		if request.header_count == MAX_HEADERS {
			return request, .Malformed, "more than 32 headers"
		}
		request.headers[request.header_count] = {strings.trim_space(name), strings.trim_space(value)}
		request.header_count += 1
	}

	if length_text, has_length := find_header(&request, "Content-Length"); has_length {
		length, ok := int_from_digits(length_text)
		if !ok {
			return request, .Malformed, fmt.tprintf("invalid Content-Length %q", length_text)
		}
		request.content_length = length
	}

	body_length := max(request.content_length, 0)
	if len(request.body) < body_length {
		return request, .Incomplete, ""
	}
	if len(request.body) > body_length {
		return request, .Malformed, "more bytes than Content-Length"
	}
	return request, .Complete, ""
}

find_header :: proc(request: ^Request, name: string) -> (value: string, ok: bool) {
	for header in request.headers[:request.header_count] {
		if strings.equal_fold(header.name, name) {
			return header.value, true
		}
	}
	return "", false
}

// The spec's request rules, checked in order. Returns the first one broken,
// or "" when the request is fine.
check_request :: proc(request: ^Request) -> (violation: string) {
	if request.method != "GET" && request.method != "PUT" {
		return fmt.tprintf("method %s", request.method)
	}
	if request.path != "/elgato/lights" {
		return fmt.tprintf("path %s", request.path)
	}
	if request.version != "HTTP/1.1" {
		return fmt.tprintf("version %s", request.version)
	}
	connection, _ := find_header(request, "Connection")
	if !strings.equal_fold(connection, "close") {
		return "no Connection: close"
	}
	if _, has_host := find_header(request, "Host"); !has_host {
		return "no Host"
	}

	if request.method == "GET" {
		if len(request.body) > 0 {
			return "GET with a body"
		}
		return ""
	}

	content_type, has_type := find_header(request, "Content-Type")
	if !has_type {
		return "PUT without Content-Type"
	}
	if !strings.equal_fold(content_type, "application/json") {
		return fmt.tprintf("Content-Type %s", content_type)
	}
	if request.content_length < 0 {
		return "PUT without Content-Length"
	}
	_, body_violation := parse_body(request.body)
	return body_violation
}

// The spec's body, byte for byte:
// {"lights":[{"on":<0|1>,"brightness":<3-100>,"temperature":<143-344>}]}
// brightness and temperature are optional, the order is fixed.
parse_body :: proc(body: string) -> (update: Light_Update, violation: string) {
	rest := body

	on := 0
	if !skip_prefix(&rest, `{"lights":[{"on":`) || !take_int(&rest, &on) || on > 1 {
		return {}, fmt.tprintf("unexpected body %s", body)
	}
	update.on = on

	if skip_prefix(&rest, `,"brightness":`) {
		brightness := 0
		if !take_int(&rest, &brightness) {
			return {}, fmt.tprintf("unexpected body %s", body)
		}
		if brightness < 3 || brightness > 100 {
			return {}, fmt.tprintf("brightness %d out of range", brightness)
		}
		update.brightness = brightness
	}

	if skip_prefix(&rest, `,"temperature":`) {
		temperature := 0
		if !take_int(&rest, &temperature) {
			return {}, fmt.tprintf("unexpected body %s", body)
		}
		if temperature < 143 || temperature > 344 {
			return {}, fmt.tprintf("temperature %d out of range", temperature)
		}
		update.temperature = temperature
	}

	if rest != "}]}" {
		return {}, fmt.tprintf("unexpected body %s", body)
	}
	return update, ""
}

@(private = "file")
skip_prefix :: proc(s: ^string, prefix: string) -> bool {
	if !strings.has_prefix(s^, prefix) {
		return false
	}
	s^ = s[len(prefix):]
	return true
}

// A JSON integer the way an encoder writes it: digits, no sign, no leading
// zero. At most 6 digits, more than any valid value needs.
@(private = "file")
take_int :: proc(s: ^string, value: ^int) -> bool {
	digits := 0
	for digits < len(s) && s[digits] >= '0' && s[digits] <= '9' {
		digits += 1
	}
	if digits == 0 || digits > 6 || (digits > 1 && s[0] == '0') {
		return false
	}
	value^, _ = int_from_digits(s[:digits])
	s^ = s[digits:]
	return true
}

// Only plain digits, unlike strconv.parse_int, which also takes signs,
// underscores and base prefixes
int_from_digits :: proc(s: string) -> (value: int, ok: bool) {
	if len(s) == 0 || len(s) > 9 {
		return 0, false
	}
	for c in transmute([]u8)s {
		if c < '0' || c > '9' {
			return 0, false
		}
		value = value * 10 + int(c - '0')
	}
	return value, true
}
