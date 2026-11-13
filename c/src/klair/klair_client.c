
internal Retry retry_make(void) {
    Retry retry = {.attempts_left = ATTEMPT_COUNT, .backoff_us = FIRST_BACKOFF_US};
    return retry;
}

internal bool retry_advance(Retry *retry, u64 *wait_us) {
    Assert(retry->attempts_left > 0);
    retry->attempts_left -= 1;
    if(retry->attempts_left == 0) {
        return false;
    }
    *wait_us = retry->backoff_us;
    retry->backoff_us *= 2;
    return true;
}


internal void job_close_socket(NET_Loop *loop, Job *job) {
    if(job->has_socket) {
        net_close(loop, job->socket);
        job->has_socket = false;
    }
}

internal void job_fail(NET_Loop *loop, Job *job, LightError error, i64 status) {
    job_close_socket(loop, job);
    job->error = error;
    job->status = status;

    u64 wait_us = 0;
    if(retry_advance(&job->retry, &wait_us)) {
        job->state = JobState_Waiting;
        job->wake_at_us = os_now_us() + wait_us;
    } else {
        job->state = JobState_Done;
    }
}

internal void job_start_attempt(NET_Loop *loop, Job *job, u64 now_us) {
    arena_clear(job->response_arena);
    job->response_start = arena_pos(job->response_arena);
    job->response = str8(arena_push(job->response_arena, 0, 1, false), 0);

    // One deadline covers the full attempt.
    job->deadline_us = now_us + ATTEMPT_TIMEOUT_US;
    job->state = JobState_Dialing;
    net_dial(loop, job->light->net_address, job->deadline_us, job);
}

internal void job_receive(NET_Loop *loop, Job *job) {
    u8 *chunk = arena_push(job->response_arena, READ_CHUNK_SIZE, 1, false);
    Assert(chunk == job->response.str + job->response.size);
    net_recv(loop, job->socket, chunk, READ_CHUNK_SIZE, job->deadline_us, job);
}

internal void job_finish(NET_Loop *loop, Job *job, Response response) {
    job_close_socket(loop, job);
    if(response.status != 200) {
        job_fail(loop, job, LightError_Status, response.status);
        return;
    }
    if(!light_state_from_json(response.body, &job->light_state)) {
        job_fail(loop, job, LightError_Invalid, 0);
        return;
    }
    job->ok = true;
    job->error = LightError_None;
    job->state = JobState_Done;
}

internal void job_complete(NET_Loop *loop, Job *job, NET_Completion *completion) {
    bool timed_out = completion->result == NET_Result_Timeout;

    switch(completion->kind) {
    case NET_OpKind_Dial: {
        if(completion->result != NET_Result_Ok) {
            job_fail(loop, job, timed_out ? LightError_Timeout : LightError_ConnectFailed, 0);
            return;
        }
        job->has_socket = true;
        job->socket = completion->socket;
        job->state = JobState_Sending;
        net_send(loop, job->socket, job->request, job->deadline_us, job);
    } break;

    case NET_OpKind_Send: {
        if(completion->result != NET_Result_Ok) {
            job_fail(loop, job, timed_out ? LightError_Timeout : LightError_ConnectFailed, 0);
            return;
        }
        job->state = JobState_Receiving;
        job_receive(loop, job);
    } break;

    case NET_OpKind_Recv: {
        if(completion->result != NET_Result_Ok) {
            job_fail(loop, job, timed_out ? LightError_Timeout : LightError_Closed, 0);
            return;
        }

        job->response.size += completion->size;
        arena_pop_to(job->response_arena, job->response_start + job->response.size);
        u64 header_end = str8_find_needle(job->response, 0, str8_lit("\r\n\r\n"));
        bool has_headers = header_end < job->response.size;
        u64 header_size = has_headers ? header_end + 4 : job->response.size;
        u64 body_size = has_headers ? job->response.size - (header_end + 4) : 0;
        if(header_size > MAX_HEADER_SIZE || body_size > MAX_BODY_SIZE) {
            job_fail(loop, job, LightError_Invalid, 0);
            return;
        }

        bool closed = completion->size == 0;
        Response response = {0};
        switch(response_parse(job->response, closed, &response)) {
        case ResponseParse_Incomplete: job_receive(loop, job); break;
        case ResponseParse_Complete: job_finish(loop, job, response); break;
        case ResponseParse_ClosedEarly: job_fail(loop, job, LightError_Closed, 0); break;
        case ResponseParse_Invalid: job_fail(loop, job, LightError_Invalid, 0); break;
        }
    } break;
    }
}


internal void jobs_run(Arena *arena, Job *jobs, u64 count) {
    NET_Loop *loop = net_loop_alloc(arena);

    u64 now_us = os_now_us();
    for(u64 i = 0; i < count; i += 1) {
        jobs[i].retry = retry_make();
        jobs[i].state = JobState_Waiting;
        jobs[i].wake_at_us = now_us;
    }

    for(;;) {
        now_us = os_now_us();
        bool all_done = true;
        u64 wake_at_us = UINT64_MAX;
        for(u64 i = 0; i < count; i += 1) {
            Job *job = &jobs[i];
            if(job->state == JobState_Waiting && job->wake_at_us <= now_us) {
                job_start_attempt(loop, job, now_us);
            }
            if(job->state == JobState_Waiting) {
                wake_at_us = Min(wake_at_us, job->wake_at_us);
            }
            if(job->state != JobState_Done) {
                all_done = false;
            }
        }
        if(all_done) {
            break;
        }

        NET_Completion completions[64];
        u64 completion_count = net_wait(loop, wake_at_us, completions, ArrayCount(completions));
        for(u64 i = 0; i < completion_count; i += 1) {
            job_complete(loop, (Job *)completions[i].user, &completions[i]);
        }
    }

    net_loop_release(loop);
}

internal String8 message_from_job(Arena *arena, Job *job) {
    switch(job->error) {
    case LightError_None: break;
    case LightError_ConnectFailed: return str8_lit("failed to connect");
    case LightError_Timeout: return str8_lit("timed out");
    case LightError_Closed: return str8_lit("connection closed unexpectedly");
    case LightError_Status: return push_str8f(arena, "unexpected status %" PRId64, job->status);
    case LightError_Invalid: return str8_lit("invalid response");
    }
    return str8_zero();
}
