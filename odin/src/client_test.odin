package klair

import vmem "core:mem/virtual"
import "core:testing"
import "core:time"

@(test)
retry_backs_off_then_gives_up :: proc(t: ^testing.T) {
	retry := Retry{attempts_left = ATTEMPTS, backoff = FIRST_BACKOFF}

	wait, again := retry_advance(&retry)
	testing.expect(t, again)
	testing.expect_value(t, wait, 100 * time.Millisecond)

	wait, again = retry_advance(&retry)
	testing.expect(t, again)
	testing.expect_value(t, wait, 200 * time.Millisecond)

	_, again = retry_advance(&retry)
	testing.expect(t, !again)
}

@(test)
messages_are_fixed :: proc(t: ^testing.T) {
	arena: vmem.Arena
	context.allocator = test_allocator(&arena)
	defer vmem.arena_destroy(&arena)

	job: Job
	Case :: struct {
		kind:    Error_Kind,
		message: string,
	}
	cases := [?]Case {
		{.Connect_Failed, "failed to connect"},
		{.Timed_Out, "timed out"},
		{.Closed_Early, "connection closed unexpectedly"},
		{.Invalid_Response, "invalid response"},
	}
	for c in cases {
		job.error_kind = c.kind
		testing.expect_value(t, message_from_job(&job), c.message)
	}

	job.error_kind = .Bad_Status
	job.status = 500
	testing.expect_value(t, message_from_job(&job), "unexpected status 500")
}
