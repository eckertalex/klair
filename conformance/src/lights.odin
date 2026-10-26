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
    No_Length, // answers without Content-Length, then closes
    Flaky,     // drops the first connection after each reset
    Trickle,   // answers one byte every 10ms
    Error_500, // always 500
    Garbage,   // 200 with a body that isn't JSON
    Zero_Temp, // 200 with "temperature":0
    Hangup,    // promises 1000 bytes, send half the body, closes
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

// Everything the light threads and the control thread share. One lock, held
// only while reading or writing these fields, never while sending.
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


serve_light :: proc(listener: net.TCP_Socket, behavior: Behavior) {
    for {
        client, _, err := net.accept_tcp(listener)
        if err != nil {
            continue
        }

        net.set_option(client, .Receive_Timeout, READ_TIMEOUT)
        // handle_light(client, behavior)
        fmt.println("TODO: implement handle_light")
        net.close(client)
        free_all(context.temp_allocator)
    }
}
