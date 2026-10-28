package conformance

import "core:fmt"
import "core:net"
import "core:os"
import "core:slice"
import "core:strings"
import "core:sync"
import "core:thread"
import "core:time"

// The lights: one thread per light, blocking sockets, one connection at a
// time. klair never sends one light two requests at once.

Behavior :: enum {
	Normal,    // answers with a Content-Length
	No_Length, // answers without one, then closes
	Flaky,     // drops the first connection after each reset
	Trickle,   // answers one byte every 10 ms
	Error_500, // always 500, and doesn't apply a PUT
	Garbage,   // 200 with a body that isn't JSON
	Zero_Temp, // 200 with "temperature":0
	Hangup,    // promises 1000 bytes, sends half a body, closes
	Silent,    // reads the request, never answers
}

// A variable, not a constant: constants can't be indexed with a runtime value
light_names := [Behavior]string {
	.Normal    = "normal",
	.No_Length = "nolen",
	.Flaky     = "flaky",
	.Trickle   = "trickle",
	.Error_500 = "error500",
	.Garbage   = "garbage",
	.Zero_Temp = "zerotemp",
	.Hangup    = "hangup",
	.Silent    = "silent",
}

LIGHT_PORTS :: [Behavior]int {
	.Normal    = 19123,
	.No_Length = 19124,
	.Flaky     = 19125,
	.Trickle   = 19126,
	.Error_500 = 19127,
	.Garbage   = 19128,
	.Zero_Temp = 19129,
	.Hangup    = 19130,
	.Silent    = 19131,
}

MAX_REQUEST :: 64 * 1024
// Shorter than klair's 3s per attempt, so a request that stops halfway is
// reported before klair gives up on it
READ_TIMEOUT :: 2 * time.Second
TRICKLE_DELAY :: 10 * time.Millisecond

// Everything the light threads and the checker share. One lock, held only
// while reading or writing these fields, never while sending.
Server :: struct {
	lock:          sync.Mutex,
	states:        [Behavior]Light_State,
	flaky_dropped: bool, // since the last reset
	log:           strings.Builder,
	echo:          bool, // also print each log line, for serve
}

server: Server

start_lights :: proc() {
	server.log = strings.builder_make()
	reset_lights()
	for port, behavior in LIGHT_PORTS {
		listener, err := net.listen_tcp({net.IP4_Loopback, port})
		if err != nil {
			fmt.eprintfln("cannot listen on port %d: %v. Is conformance serve still running?", port, err)
			os.exit(2)
		}
		thread.create_and_start_with_poly_data2(listener, behavior, serve_light)
	}
}

// Every light back to its default state, flaky drops its next connection
// again, the log is emptied
reset_lights :: proc() {
	sync.guard(&server.lock)
	for &state in server.states {
		state = DEFAULT_STATE
	}
	server.flaky_dropped = false
	strings.builder_reset(&server.log)
}

// Every request since the last reset, one per line, sorted: requests to
// different lights arrive in any order
light_log :: proc(allocator := context.temp_allocator) -> []string {
	sync.guard(&server.lock)
	text := strings.trim_suffix(strings.to_string(server.log), "\n")
	if text == "" {
		return nil
	}
	lines := strings.split_lines(strings.clone(text, allocator), allocator)
	slice.sort(lines)
	return lines
}

// Call with server.lock held
log_line :: proc(format: string, args: ..any) {
	line := fmt.tprintf(format, ..args)
	strings.write_string(&server.log, line)
	strings.write_byte(&server.log, '\n')
	if server.echo {
		fmt.println(line)
	}
}

serve_light :: proc(listener: net.TCP_Socket, behavior: Behavior) {
	for {
		client, _, err := net.accept_tcp(listener)
		if err != nil {
			continue
		}
		net.set_option(client, .Receive_Timeout, READ_TIMEOUT)
		handle_light(client, behavior)
		net.close(client)
		free_all(context.temp_allocator)
	}
}

handle_light :: proc(client: net.TCP_Socket, behavior: Behavior) {
	name := light_names[behavior]

	if behavior == .Flaky {
		sync.mutex_lock(&server.lock)
		drop := !server.flaky_dropped
		server.flaky_dropped = true
		if drop {
			log_line("%s dropped", name)
		}
		sync.mutex_unlock(&server.lock)
		if drop {
			return
		}
	}

	request, problem := read_request(client)
	if problem == "" {
		problem = check_request(&request)
	}
	if problem != "" {
		sync.mutex_lock(&server.lock)
		log_line("%s violation: %s", name, problem)
		sync.mutex_unlock(&server.lock)
		send(client, build_response("400 Bad Request", "", true))
		return
	}

	sync.mutex_lock(&server.lock)
	if request.method == "PUT" {
		log_line("%s PUT %s", name, request.body)
		update, _ := parse_body(request.body)
		if behavior != .Error_500 {
			apply_update(&server.states[behavior], update)
		}
	} else {
		log_line("%s GET", name)
	}
	state := server.states[behavior]
	sync.mutex_unlock(&server.lock)

	switch behavior {
	case .Normal, .Flaky:
		send(client, build_response("200 OK", json_from_state(state), true))
	case .No_Length:
		send(client, build_response("200 OK", json_from_state(state), false))
	case .Trickle:
		response := build_response("200 OK", json_from_state(state), true)
		for i in 0 ..< len(response) {
			send(client, response[i:i + 1]) or_break
			time.sleep(TRICKLE_DELAY)
		}
	case .Error_500:
		send(client, build_response("500 Internal Server Error", "", true))
	case .Garbage:
		send(client, build_response("200 OK", "not json", true))
	case .Zero_Temp:
		state.temperature = 0
		send(client, build_response("200 OK", json_from_state(state), true))
	case .Hangup:
		body := json_from_state(state)
		head := build_response("200 OK", "", false)
		// Swap the end of the headers for a Content-Length that's never met
		head = head[:len(head) - len("\r\n")]
		send(client, fmt.tprintf("%sContent-Length: 1000\r\n\r\n%s", head, body[:len(body) / 2]))
	case .Silent:
		wait_for_close(client)
	}
}

// Reads until parse_request says the request is complete. A request that
// stops halfway, by closing or by going quiet for READ_TIMEOUT, is a
// violation too.
read_request :: proc(client: net.TCP_Socket) -> (request: Request, problem: string) {
	data := make([dynamic]u8, 0, 1024, context.temp_allocator)
	chunk: [1024]u8
	for {
		status: Parse_Status
		request, status, problem = parse_request(string(data[:]))
		if status != .Incomplete {
			return
		}

		n, err := net.recv_tcp(client, chunk[:])
		if err != nil || n == 0 {
			if request.content_length >= 0 {
				return request, fmt.tprintf(
					"Content-Length %d but body has %d bytes",
					request.content_length,
					len(request.body),
				)
			}
			return request, "incomplete request"
		}
		append(&data, ..chunk[:n])
		if len(data) > MAX_REQUEST {
			return request, "request larger than 64 KiB"
		}
	}
}

// Until klair gives up and closes. The read timeout only wakes us up.
wait_for_close :: proc(client: net.TCP_Socket) {
	chunk: [1024]u8
	for {
		n, err := net.recv_tcp(client, chunk[:])
		if err == .Would_Block || err == .Timeout || err == .Interrupted {
			continue
		}
		if err != nil || n == 0 {
			return
		}
	}
}

// false once klair has hung up
send :: proc(client: net.TCP_Socket, text: string) -> bool {
	_, err := net.send_tcp(client, transmute([]u8)text)
	return err == nil
}
