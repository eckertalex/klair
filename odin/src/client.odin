package klair

import "core:fmt"
import "core:nbio"
import "core:net"
import "core:strings"
import "core:time"

ATTEMPTS :: 3
ATTEMPT_BUDGET :: 3 * time.Second
FIRST_BACKOFF :: 100 * time.Millisecond
MAX_HEADER :: 64 * 1024
MAX_BODY :: 64 * 1024
RECV_CHUNK :: 1024

Error_Kind :: enum {
	None,
	Connect_Failed,
	Timed_Out,
	Closed_Early,
	Bad_Status,
	Invalid_Response,
}

Retry :: struct {
	attempts_left: int,
	backoff:       time.Duration,
}

Job :: struct {
	light:      ^Light,
	request:    string,
	retry:      Retry,
	deadline:   time.Time,
	socket:     net.TCP_Socket,
	connected:  bool,
	response:   [dynamic]u8,
	chunk:      [RECV_CHUNK]u8,

	ok:         bool,
	state:      Light_State,
	error_kind: Error_Kind,
	status:     int, // for .Bad_Status
}

retry_advance :: proc(retry: ^Retry) -> (wait: time.Duration, again: bool) {
	assert(retry.attempts_left > 0)
	retry.attempts_left -= 1
	if retry.attempts_left == 0 {
		return 0, false
	}
	wait = retry.backoff
	retry.backoff *= 2
	return wait, true
}

job_init :: proc(job: ^Job, light: ^Light, method: Method, settings: Settings) {
	job.light = light
	job.request = request_from_method(method, light.host, settings)
	job.retry = {attempts_left = ATTEMPTS, backoff = FIRST_BACKOFF}
}

run_jobs :: proc(jobs: []Job) {
	ensure(nbio.acquire_thread_event_loop() == nil)
	defer nbio.release_thread_event_loop()

	for &job in jobs {
		start_attempt(&job)
	}
	ensure(nbio.run() == nil)
}

message_from_job :: proc(job: ^Job) -> string {
	switch job.error_kind {
	case .Connect_Failed:
		return "failed to connect"
	case .Timed_Out:
		return "timed out"
	case .Closed_Early:
		return "connection closed unexpectedly"
	case .Bad_Status:
		return fmt.aprintf("unexpected status %d", job.status)
	case .Invalid_Response:
		return "invalid response"
	case .None:
	}
	unreachable()
}

@(private = "file")
start_attempt :: proc(job: ^Job) {
	clear(&job.response)
	job.deadline = time.time_add(nbio.now(), ATTEMPT_BUDGET)
	nbio.dial_poly(job.light.endpoint, job, on_dial, ATTEMPT_BUDGET)
}

@(private = "file")
time_left :: proc(job: ^Job) -> (left: time.Duration, ok: bool) {
	left = time.diff(nbio.now(), job.deadline)
	if left <= 0 {
		fail(job, .Timed_Out)
		return 0, false
	}
	return left, true
}

@(private = "file")
on_dial :: proc(op: ^nbio.Operation, job: ^Job) {
	if op.dial.err != nil {
		fail(job, .Timed_Out if op.dial.err == net.Dial_Error.Timeout else .Connect_Failed)
		return
	}
	job.socket = op.dial.socket
	job.connected = true

	left, in_time := time_left(job)
	if !in_time {
		return
	}
	nbio.send_poly(job.socket, {transmute([]u8)job.request}, job, on_send, timeout = left)
}

@(private = "file")
on_send :: proc(op: ^nbio.Operation, job: ^Job) {
	if op.send.err != nil {
		fail(job, .Timed_Out if op.send.err == net.TCP_Send_Error.Timeout else .Connect_Failed)
		return
	}
	receive(job)
}

@(private = "file")
receive :: proc(job: ^Job) {
	left, in_time := time_left(job)
	if !in_time {
		return
	}
	nbio.recv_poly(job.socket, {job.chunk[:]}, job, on_recv, timeout = left)
}

@(private = "file")
on_recv :: proc(op: ^nbio.Operation, job: ^Job) {
	if op.recv.err != nil {
		fail(job, .Timed_Out if op.recv.err == net.TCP_Recv_Error.Timeout else .Closed_Early)
		return
	}

	closed := op.recv.received == 0
	append(&job.response, ..job.chunk[:op.recv.received])
	header_end := strings.index(string(job.response[:]), "\r\n\r\n")
	header_size := len(job.response) if header_end < 0 else header_end + len("\r\n\r\n")
	body_size := 0 if header_end < 0 else len(job.response) - header_size
	if header_size > MAX_HEADER || body_size > MAX_BODY {
		fail(job, .Invalid_Response)
		return
	}

	response, check := check_response(string(job.response[:]), closed)
	switch check {
	case .Incomplete:
		receive(job)
	case .Closed_Early:
		fail(job, .Closed_Early)
	case .Malformed:
		fail(job, .Invalid_Response)
	case .Complete:
		finish(job, response)
	}
}

@(private = "file")
finish :: proc(job: ^Job, response: Response) {
	if response.status != 200 {
		job.status = response.status
		fail(job, .Bad_Status)
		return
	}
	state, ok := light_state_from_body(response.body)
	if !ok {
		fail(job, .Invalid_Response)
		return
	}

	close_socket(job)
	job.ok = true
	job.state = state
	job.error_kind = .None
}

@(private = "file")
fail :: proc(job: ^Job, kind: Error_Kind) {
	close_socket(job)
	job.error_kind = kind

	wait, again := retry_advance(&job.retry)
	if !again {
		return
	}
	nbio.timeout_poly(wait, job, proc(_: ^nbio.Operation, job: ^Job) {
		start_attempt(job)
	})
}

@(private = "file")
close_socket :: proc(job: ^Job) {
	if job.connected {
		nbio.close(job.socket)
		job.connected = false
	}
}
