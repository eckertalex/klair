
internal long net_linux_setup(u32 entries, struct io_uring_params *params) {
    return syscall(__NR_io_uring_setup, entries, params);
}

internal long net_linux_enter(int fd, u32 submit, u32 wait_count, u32 flags) {
    return syscall(__NR_io_uring_enter, fd, submit, wait_count, flags, nullptr, 0);
}

internal struct io_uring_sqe *net_linux_sqe(NET_Loop *loop) {
    u32 tail = __atomic_load_n(loop->sq_tail, __ATOMIC_RELAXED);
    u32 head = __atomic_load_n(loop->sq_head, __ATOMIC_ACQUIRE);
    AssertAlways(tail - head < *loop->sq_entries);
    u32 index = tail & *loop->sq_mask;
    struct io_uring_sqe *sqe = &loop->sqes[index];
    MemoryZero(sqe, sizeof(*sqe));
    loop->sq_array[index] = index;
    __atomic_store_n(loop->sq_tail, tail + 1, __ATOMIC_RELEASE);
    return sqe;
}

internal void net_linux_submit(NET_Loop *loop) {
    u32 head = __atomic_load_n(loop->sq_head, __ATOMIC_ACQUIRE);
    u32 tail = __atomic_load_n(loop->sq_tail, __ATOMIC_ACQUIRE);
    while(head != tail) {
        long result = net_linux_enter(loop->ring_fd, tail - head, 0, 0);
        if(result < 0 && errno == EINTR) continue;
        AssertAlways(result >= 0);
        if(result == 0) continue;
        head += (u32)result;
    }
}

internal void net_linux_list_push(NET_LINUX_OpList *list, NET_LINUX_Op *op) {
    op->next = nullptr;
    op->prev = list->last;
    if(list->last) list->last->next = op;
    else list->first = op;
    list->last = op;
}

internal void net_linux_list_remove(NET_LINUX_OpList *list, NET_LINUX_Op *op) {
    if(op->prev) op->prev->next = op->next;
    else list->first = op->next;
    if(op->next) op->next->prev = op->prev;
    else list->last = op->prev;
    op->next = nullptr;
    op->prev = nullptr;
}

internal NET_LINUX_Op *net_linux_op_alloc(NET_Loop *loop, NET_OpKind kind, int fd, u64 deadline_us, void *user) {
    NET_LINUX_Op *op = loop->first_free;
    if(op) loop->first_free = op->next;
    else op = push_array_no_zero(loop->arena, NET_LINUX_Op, 1);
    MemoryZero(op, sizeof(*op));
    op->kind = kind;
    op->fd = fd;
    op->deadline_us = deadline_us;
    op->user = user;
    net_linux_list_push(&loop->pending, op);
    return op;
}

internal void net_linux_queue_cancel(NET_Loop *loop, NET_LINUX_Op *op) {
    if(op->cancel_sent || op->main_done) {
        op->cancel_done = true;
        return;
    }
    struct io_uring_sqe *sqe = net_linux_sqe(loop);
    sqe->opcode = IORING_OP_ASYNC_CANCEL;
    sqe->addr = (u64)(uintptr_t)op;
    sqe->user_data = ((u64)(uintptr_t)op) | 1;
    op->cancel_sent = true;
}

internal void net_linux_queue_op(NET_Loop *loop, NET_LINUX_Op *op) {
    struct io_uring_sqe *sqe = net_linux_sqe(loop);
    sqe->fd = op->fd;
    sqe->user_data = (u64)(uintptr_t)op;
    switch(op->kind) {
    case NET_OpKind_Dial:
        sqe->opcode = IORING_OP_CONNECT;
        sqe->addr = (u64)(uintptr_t)&op->address;
        sqe->off = (u64)op->address_size;
        break;
    case NET_OpKind_Send:
        sqe->opcode = IORING_OP_SEND;
        sqe->addr = (u64)(uintptr_t)(op->data.str + op->sent);
        sqe->len = (u32)(op->data.size - op->sent);
        sqe->msg_flags = MSG_NOSIGNAL;
        break;
    case NET_OpKind_Recv:
        sqe->opcode = IORING_OP_RECV;
        sqe->addr = (u64)(uintptr_t)op->buffer;
        sqe->len = (u32)op->capacity;
        break;
    }
}

internal void net_linux_free_op(NET_Loop *loop, NET_LINUX_Op *op) {
    net_linux_list_remove(&loop->pending, op);
    op->next = loop->first_free;
    loop->first_free = op;
}

internal void net_linux_finish_op(NET_Loop *loop, NET_LINUX_Op *op) {
    if(op->finished || !op->main_done) return;
    if(op->cancel_sent && !op->cancel_done) return;
    if(op->drop) {
        if(op->kind == NET_OpKind_Dial && op->fd >= 0) close(op->fd);
        net_linux_free_op(loop, op);
        return;
    }
    NET_Result result = op->timed_out ? NET_Result_Timeout :
        (op->failed || op->result < 0 ? NET_Result_Failed : NET_Result_Ok);
    NET_Socket socket = {0};
    u64 size = 0;
    if(op->kind == NET_OpKind_Dial) {
        if(result == NET_Result_Ok) socket.u64[0] = (u64)op->fd;
        else if(op->fd >= 0) { close(op->fd); op->fd = -1; }
    } else if(result == NET_Result_Ok) {
        size = (u64)op->result;
    }
    op->completion = (NET_Completion){.kind=op->kind, .result=result, .user=op->user, .socket=socket, .size=size};
    net_linux_list_remove(&loop->pending, op);
    net_linux_list_push(&loop->finished, op);
    op->finished = true;
}

internal void net_linux_cqe(NET_Loop *loop, struct io_uring_cqe cqe) {
    if(cqe.user_data & 1) {
        NET_LINUX_Op *op = (NET_LINUX_Op *)(uintptr_t)(cqe.user_data & ~(u64)1);
        op->cancel_done = true;
        net_linux_finish_op(loop, op);
        return;
    }
    NET_LINUX_Op *op = (NET_LINUX_Op *)(uintptr_t)cqe.user_data;
    if(op->drop) {
        op->main_done = true;
    } else if(op->timed_out) {
        op->main_done = true;
    } else if(cqe.res < 0) {
        if((op->kind == NET_OpKind_Send || op->kind == NET_OpKind_Recv) &&
           (cqe.res == -EAGAIN || cqe.res == -EINTR)) {
            net_linux_queue_op(loop, op);
        } else {
            op->failed = true;
            op->main_done = true;
        }
    } else if(op->kind == NET_OpKind_Send && (u64)cqe.res < op->data.size - op->sent) {
        op->sent += (u64)cqe.res;
        if(cqe.res == 0) { op->failed = true; op->main_done = true; }
        else net_linux_queue_op(loop, op);
    } else {
        op->result = cqe.res;
        op->main_done = true;
    }
    net_linux_finish_op(loop, op);
}

internal void net_linux_reap(NET_Loop *loop) {
    u32 head = __atomic_load_n(loop->cq_head, __ATOMIC_RELAXED);
    u32 tail = __atomic_load_n(loop->cq_tail, __ATOMIC_ACQUIRE);
    while(head != tail) {
        struct io_uring_cqe cqe = loop->cqes[head & *loop->cq_mask];
        head += 1;
        __atomic_store_n(loop->cq_head, head, __ATOMIC_RELEASE);
        net_linux_cqe(loop, cqe);
        tail = __atomic_load_n(loop->cq_tail, __ATOMIC_ACQUIRE);
    }
}

internal struct sockaddr_storage net_linux_sockaddr(NET_Address address, socklen_t *size) {
    struct sockaddr_storage storage;
    MemoryZero(&storage, sizeof(storage));
    if(address.is_ip6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&storage;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(address.port);
        MemoryCopy(&in6->sin6_addr, address.ip, sizeof(in6->sin6_addr));
        *size = sizeof(*in6);
    } else {
        struct sockaddr_in *in = (struct sockaddr_in *)&storage;
        in->sin_family = AF_INET;
        in->sin_port = htons(address.port);
        MemoryCopy(&in->sin_addr, address.ip, sizeof(in->sin_addr));
        *size = sizeof(*in);
    }
    return storage;
}


internal bool net_address_from_str8(String8 ip, bool is_ip6, u16 port, NET_Address *out) {
    Temp scratch = scratch_begin(nullptr, 0);
    String8 text = push_str8_copy(scratch.arena, ip);
    NET_Address address = {.is_ip6=is_ip6, .port=port};
    bool ok = inet_pton(is_ip6 ? AF_INET6 : AF_INET, (char *)text.str, address.ip) == 1;
    scratch_end(scratch);
    if(ok) *out = address;
    return ok;
}

internal NET_Loop *net_loop_alloc(Arena *arena) {
    NET_Loop *loop = push_array(arena, NET_Loop, 1);
    loop->arena = arena;
    loop->ring_fd = (int)net_linux_setup(NET_LINUX_RING_ENTRIES, &loop->params);
    AssertAlways(loop->ring_fd >= 0);
    loop->sq_ring_size = loop->params.sq_off.array + loop->params.sq_entries * sizeof(u32);
    loop->cq_ring_size = loop->params.cq_off.cqes + loop->params.cq_entries * sizeof(struct io_uring_cqe);
    if(loop->params.features & IORING_FEAT_SINGLE_MMAP) {
        loop->sq_ring_size = Max(loop->sq_ring_size, loop->cq_ring_size);
        loop->cq_ring_size = loop->sq_ring_size;
    }
    loop->sq_ring = mmap(nullptr, loop->sq_ring_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, loop->ring_fd, IORING_OFF_SQ_RING);
    AssertAlways(loop->sq_ring != MAP_FAILED);
    if(loop->params.features & IORING_FEAT_SINGLE_MMAP) loop->cq_ring = loop->sq_ring;
    else {
        loop->cq_ring = mmap(nullptr, loop->cq_ring_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, loop->ring_fd, IORING_OFF_CQ_RING);
        AssertAlways(loop->cq_ring != MAP_FAILED);
    }
    loop->sqes_size = loop->params.sq_entries * sizeof(struct io_uring_sqe);
    loop->sqes = mmap(nullptr, loop->sqes_size, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_POPULATE, loop->ring_fd, IORING_OFF_SQES);
    AssertAlways(loop->sqes != MAP_FAILED);
    u8 *sq = loop->sq_ring, *cq = loop->cq_ring;
    loop->sq_head = (u32 *)(sq + loop->params.sq_off.head);
    loop->sq_tail = (u32 *)(sq + loop->params.sq_off.tail);
    loop->sq_mask = (u32 *)(sq + loop->params.sq_off.ring_mask);
    loop->sq_entries = (u32 *)(sq + loop->params.sq_off.ring_entries);
    loop->sq_array = (u32 *)(sq + loop->params.sq_off.array);
    loop->cq_head = (u32 *)(cq + loop->params.cq_off.head);
    loop->cq_tail = (u32 *)(cq + loop->params.cq_off.tail);
    loop->cq_mask = (u32 *)(cq + loop->params.cq_off.ring_mask);
    loop->cqes = (struct io_uring_cqe *)(cq + loop->params.cq_off.cqes);
    return loop;
}

internal void net_loop_release(NET_Loop *loop) {
    if(loop->sqes && loop->sqes != MAP_FAILED) munmap(loop->sqes, loop->sqes_size);
    if(loop->cq_ring && loop->cq_ring != loop->sq_ring && loop->cq_ring != MAP_FAILED) munmap(loop->cq_ring, loop->cq_ring_size);
    if(loop->sq_ring && loop->sq_ring != MAP_FAILED) munmap(loop->sq_ring, loop->sq_ring_size);
    close(loop->ring_fd);
}

internal void net_dial(NET_Loop *loop, NET_Address address, u64 deadline_us, void *user) {
    socklen_t address_size = 0;
    struct sockaddr_storage storage = net_linux_sockaddr(address, &address_size);
    NET_LINUX_Op *op = net_linux_op_alloc(loop, NET_OpKind_Dial, -1, deadline_us, user);
    op->fd = socket(storage.ss_family, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(op->fd < 0) { op->failed = true; op->main_done = true; net_linux_finish_op(loop, op); return; }
    op->address = storage;
    op->address_size = address_size;
    net_linux_queue_op(loop, op);
    net_linux_submit(loop);
}

internal void net_send(NET_Loop *loop, NET_Socket socket, String8 data, u64 deadline_us, void *user) {
    NET_LINUX_Op *op = net_linux_op_alloc(loop, NET_OpKind_Send, (int)socket.u64[0], deadline_us, user);
    op->data = data;
    net_linux_queue_op(loop, op);
    net_linux_submit(loop);
}

internal void net_recv(NET_Loop *loop, NET_Socket socket, u8 *buffer, u64 capacity, u64 deadline_us, void *user) {
    NET_LINUX_Op *op = net_linux_op_alloc(loop, NET_OpKind_Recv, (int)socket.u64[0], deadline_us, user);
    op->buffer = buffer;
    op->capacity = capacity;
    net_linux_queue_op(loop, op);
    net_linux_submit(loop);
}

internal void net_close(NET_Loop *loop, NET_Socket socket) {
    int fd = (int)socket.u64[0];
    for(NET_LINUX_Op *op = loop->pending.first, *next = nullptr; op; op = next) {
        next = op->next;
        if(op->fd == fd) { op->drop = true; net_linux_queue_cancel(loop, op); }
    }
    net_linux_submit(loop);
    for(;;) {
        bool found = false;
        for(NET_LINUX_Op *op = loop->pending.first; op; op = op->next) if(op->fd == fd) found = true;
        if(!found) break;
        struct pollfd pfd = {.fd=loop->ring_fd, .events=POLLIN};
        poll(&pfd, 1, -1);
        net_linux_reap(loop);
    }
    for(NET_LINUX_Op *op = loop->finished.first, *next = nullptr; op; op = next) {
        next = op->next;
        if(op->completion.socket.u64[0] == (u64)fd || (op->kind != NET_OpKind_Dial && op->fd == fd)) {
            net_linux_list_remove(&loop->finished, op);
            op->next = loop->first_free;
            loop->first_free = op;
        }
    }
    close(fd);
}

internal u64 net_wait(NET_Loop *loop, u64 wake_at_us, NET_Completion *completions, u64 capacity) {
    if(!loop->finished.first) {
        u64 until = wake_at_us;
        for(NET_LINUX_Op *op = loop->pending.first; op; op = op->next) until = Min(until, op->deadline_us);
        for(NET_LINUX_Op *op = loop->pending.first; op; op = op->next) {
            if(op->deadline_us <= os_now_us() && !op->cancel_sent && !op->main_done) {
                op->timed_out = true;
                net_linux_queue_cancel(loop, op);
            }
        }
        net_linux_submit(loop);
        net_linux_reap(loop);
        if(!loop->finished.first) {
            u64 now = os_now_us();
            int timeout = -1;
            if(until != UINT64_MAX) {
                u64 wait_us = until > now ? until - now : 0;
                u64 wait_ms = (wait_us + 999) / 1000;
                timeout = wait_ms > INT_MAX ? INT_MAX : (int)wait_ms;
            }
            struct pollfd pfd = {.fd=loop->ring_fd, .events=POLLIN};
            int ready;
            do { ready = poll(&pfd, 1, timeout); } while(ready < 0 && errno == EINTR);
            AssertAlways(ready >= 0);
            net_linux_reap(loop);
            now = os_now_us();
            for(NET_LINUX_Op *op = loop->pending.first; op; op = op->next) {
                if(op->deadline_us <= now && !op->cancel_sent && !op->main_done) {
                    op->timed_out = true;
                    net_linux_queue_cancel(loop, op);
                }
            }
            net_linux_submit(loop);
            if(loop->pending.first) {
                bool waiting_cancel = false;
                for(NET_LINUX_Op *op = loop->pending.first; op; op = op->next) if(op->cancel_sent && !op->cancel_done) waiting_cancel = true;
                if(waiting_cancel) {
                    struct pollfd waitfd = {.fd=loop->ring_fd, .events=POLLIN};
                    poll(&waitfd, 1, -1);
                    net_linux_reap(loop);
                }
            }
        }
    }
    u64 count = 0;
    while(loop->finished.first && count < capacity) {
        NET_LINUX_Op *op = loop->finished.first;
        net_linux_list_remove(&loop->finished, op);
        completions[count++] = op->completion;
        op->next = loop->first_free;
        loop->first_free = op;
    }
    return count;
}
