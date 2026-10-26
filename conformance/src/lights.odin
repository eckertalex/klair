
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

CONTROL_PORT :: 19100
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
}

server: Server

listen :: proc(port: int) -> net.TCP_Socket {
    listener, err := net.listen_tcp({net.IP4_Loopback, port})
    ensure(err == nil, fmt.tprintf("listening on %d: %v", port, err))
    return listener
}

// One thread per light, one connection at a time. klair never sends a light
// two requests at once.
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

// Call with server.lock held, or before the threads start
reset :: proc() {
	for &state in server.states {
		state = DEFAULT_STATE
	}
	server.flaky_dropped = false
	strings.builder_reset(&server.log)
}

serve_control :: proc(listener: net.TCP_Socket) {
	for {
		client, _, err := net.accept_tcp(listener)
		if err != nil {
			continue
		}
		net.set_option(client, .Receive_Timeout, READ_TIMEOUT)
		// handle_control(client)
        fmt.println("TODO: implement handle_control")
		net.close(client)
		free_all(context.temp_allocator)
	}
}
