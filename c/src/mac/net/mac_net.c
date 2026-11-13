
internal void net_mac_list_push(NET_MAC_OpList *list, NET_MAC_Op *op) {
    op->next = nullptr;
    op->prev = list->last;
    if(list->last) {
        list->last->next = op;
    } else {
        list->first = op;
    }
    list->last = op;
}

internal void net_mac_list_remove(NET_MAC_OpList *list, NET_MAC_Op *op) {
    if(op->prev) {
        op->prev->next = op->next;
    } else {
        list->first = op->next;
    }
    if(op->next) {
        op->next->prev = op->prev;
    } else {
        list->last = op->prev;
    }
    op->next = nullptr;
    op->prev = nullptr;
}

internal NET_MAC_Op *net_mac_op_alloc(NET_Loop *loop, NET_OpKind kind, int fd, u64 deadline_us, void *user) {
    NET_MAC_Op *op = loop->first_free;
    if(op) {
        loop->first_free = op->next;
    } else {
        op = push_array_no_zero(loop->arena, NET_MAC_Op, 1);
    }
    MemoryZero(op, sizeof(*op));
    op->kind = kind;
    op->fd = fd;
    op->deadline_us = deadline_us;
    op->user = user;
    net_mac_list_push(&loop->pending, op);
    return op;
}

internal void net_mac_op_finish(NET_Loop *loop, NET_MAC_Op *op, NET_Result result, u64 size) {
    if(op->filter != 0) {
        struct kevent change;
        EV_SET(&change, (uintptr_t)op->fd, op->filter, EV_DELETE, 0, 0, nullptr);
        kevent(loop->kq, &change, 1, nullptr, 0, nullptr);
        op->filter = 0;
    }

    NET_Socket socket = {0};
    if(op->kind == NET_OpKind_Dial) {
        if(result == NET_Result_Ok) {
            socket.u64[0] = (u64)op->fd;
        } else if(op->fd >= 0) {
            close(op->fd);
            op->fd = -1;
        }
    }

    op->completion = (NET_Completion){
        .kind = op->kind,
        .result = result,
        .user = op->user,
        .socket = socket,
        .size = size,
    };
    net_mac_list_remove(&loop->pending, op);
    net_mac_list_push(&loop->finished, op);
}

internal void net_mac_op_watch(NET_Loop *loop, NET_MAC_Op *op, i16 filter) {
    struct kevent change;
    EV_SET(&change, (uintptr_t)op->fd, filter, EV_ADD | EV_ONESHOT, 0, 0, op);
    if(kevent(loop->kq, &change, 1, nullptr, 0, nullptr) < 0) {
        net_mac_op_finish(loop, op, NET_Result_Failed, 0);
        return;
    }
    op->filter = filter;
}

internal socklen_t net_mac_sockaddr_from_address(NET_Address address, struct sockaddr_storage *out) {
    MemoryZero(out, sizeof(*out));
    if(address.is_ip6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)out;
        in6->sin6_family = AF_INET6;
        in6->sin6_port = htons(address.port);
        MemoryCopy(&in6->sin6_addr, address.ip, sizeof(in6->sin6_addr));
        return sizeof(*in6);
    }
    struct sockaddr_in *in = (struct sockaddr_in *)out;
    in->sin_family = AF_INET;
    in->sin_port = htons(address.port);
    MemoryCopy(&in->sin_addr, address.ip, sizeof(in->sin_addr));
    return sizeof(*in);
}

internal void net_mac_op_service(NET_Loop *loop, NET_MAC_Op *op) {
    op->filter = 0; // EV_ONESHOT, kqueue already dropped it

    switch(op->kind) {
    case NET_OpKind_Dial: {
        int error = 0;
        socklen_t length = sizeof(error);
        bool connected = getsockopt(op->fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 && error == 0;
        net_mac_op_finish(loop, op, connected ? NET_Result_Ok : NET_Result_Failed, 0);
    } break;

    case NET_OpKind_Send: {
        ssize_t sent = send(op->fd, op->data.str + op->sent, op->data.size - op->sent, 0);
        if(sent < 0 && (errno == EAGAIN || errno == EINTR)) {
            net_mac_op_watch(loop, op, EVFILT_WRITE);
        } else if(sent < 0) {
            net_mac_op_finish(loop, op, NET_Result_Failed, 0);
        } else {
            op->sent += (u64)sent;
            if(op->sent == op->data.size) {
                net_mac_op_finish(loop, op, NET_Result_Ok, op->sent);
            } else {
                net_mac_op_watch(loop, op, EVFILT_WRITE);
            }
        }
    } break;

    case NET_OpKind_Recv: {
        ssize_t received = recv(op->fd, op->buffer, op->capacity, 0);
        if(received < 0 && (errno == EAGAIN || errno == EINTR)) {
            net_mac_op_watch(loop, op, EVFILT_READ);
        } else if(received < 0) {
            net_mac_op_finish(loop, op, NET_Result_Failed, 0);
        } else {
            net_mac_op_finish(loop, op, NET_Result_Ok, (u64)received);
        }
    } break;
    }
}


internal bool net_address_from_str8(String8 ip, bool is_ip6, u16 port, NET_Address *out) {
    Temp scratch = scratch_begin(nullptr, 0);
    String8 ip_cstring = push_str8_copy(scratch.arena, ip);
    NET_Address address = {.is_ip6 = is_ip6, .port = port};
    bool ok = inet_pton(is_ip6 ? AF_INET6 : AF_INET, (char *)ip_cstring.str, address.ip) == 1;
    scratch_end(scratch);

    if(ok) {
        *out = address;
    }
    return ok;
}

internal NET_Loop *net_loop_alloc(Arena *arena) {
    NET_Loop *loop = push_array(arena, NET_Loop, 1);
    loop->arena = arena;
    loop->kq = kqueue();
    AssertAlways(loop->kq >= 0);
    return loop;
}

internal void net_loop_release(NET_Loop *loop) {
    close(loop->kq);
}

internal void net_dial(NET_Loop *loop, NET_Address address, u64 deadline_us, void *user) {
    NET_MAC_Op *op = net_mac_op_alloc(loop, NET_OpKind_Dial, -1, deadline_us, user);

    struct sockaddr_storage storage;
    socklen_t length = net_mac_sockaddr_from_address(address, &storage);
    op->fd = socket(storage.ss_family, SOCK_STREAM, 0);
    if(op->fd < 0) {
        net_mac_op_finish(loop, op, NET_Result_Failed, 0);
        return;
    }

    // Keep dropped connections on the normal error path, not SIGPIPE.
    int flags = fcntl(op->fd, F_GETFL, 0);
    int one = 1;
    if(flags < 0 ||
       fcntl(op->fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
       setsockopt(op->fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one)) < 0) {
        net_mac_op_finish(loop, op, NET_Result_Failed, 0);
        return;
    }

    if(connect(op->fd, (struct sockaddr *)&storage, length) == 0) {
        net_mac_op_finish(loop, op, NET_Result_Ok, 0);
    } else if(errno == EINPROGRESS) {
        net_mac_op_watch(loop, op, EVFILT_WRITE);
    } else {
        net_mac_op_finish(loop, op, NET_Result_Failed, 0);
    }
}

internal void net_send(NET_Loop *loop, NET_Socket socket, String8 data, u64 deadline_us, void *user) {
    NET_MAC_Op *op = net_mac_op_alloc(loop, NET_OpKind_Send, (int)socket.u64[0], deadline_us, user);
    op->data = data;
    net_mac_op_watch(loop, op, EVFILT_WRITE);
}

internal void net_recv(NET_Loop *loop, NET_Socket socket, u8 *buffer, u64 capacity, u64 deadline_us, void *user) {
    NET_MAC_Op *op = net_mac_op_alloc(loop, NET_OpKind_Recv, (int)socket.u64[0], deadline_us, user);
    op->buffer = buffer;
    op->capacity = capacity;
    net_mac_op_watch(loop, op, EVFILT_READ);
}

internal void net_close(NET_Loop *loop, NET_Socket socket) {
    int fd = (int)socket.u64[0];

    NET_MAC_OpList *lists[] = {&loop->pending, &loop->finished};
    for(u64 i = 0; i < ArrayCount(lists); i += 1) {
        for(NET_MAC_Op *op = lists[i]->first, *next = nullptr; op; op = next) {
            next = op->next;
            if(op->fd == fd) {
                net_mac_list_remove(lists[i], op);
                op->next = loop->first_free;
                loop->first_free = op;
            }
        }
    }
    close(fd);
}

internal u64 net_wait(NET_Loop *loop, u64 wake_at_us, NET_Completion *completions, u64 capacity) {
    if(loop->finished.first == nullptr) {
        u64 until_us = wake_at_us;
        for(NET_MAC_Op *op = loop->pending.first; op; op = op->next) {
            until_us = Min(until_us, op->deadline_us);
        }

        u64 now_us = os_now_us();
        struct timespec timeout = {0};
        if(until_us > now_us) {
            u64 wait_us = until_us - now_us;
            timeout.tv_sec = (time_t)(wait_us / Million(1));
            timeout.tv_nsec = (long)(wait_us % Million(1)) * 1000;
        }

        struct kevent events[64];
        int event_count = kevent(loop->kq, nullptr, 0, events, (int)ArrayCount(events),
                                 until_us == UINT64_MAX ? nullptr : &timeout);
        for(int i = 0; i < event_count; i += 1) {
            net_mac_op_service(loop, (NET_MAC_Op *)events[i].udata);
        }

        now_us = os_now_us();
        for(NET_MAC_Op *op = loop->pending.first, *next = nullptr; op; op = next) {
            next = op->next;
            if(op->deadline_us <= now_us) {
                net_mac_op_finish(loop, op, NET_Result_Timeout, 0);
            }
        }
    }

    u64 count = 0;
    while(loop->finished.first && count < capacity) {
        NET_MAC_Op *op = loop->finished.first;
        net_mac_list_remove(&loop->finished, op);
        completions[count] = op->completion;
        count += 1;
        op->next = loop->first_free;
        loop->first_free = op;
    }
    return count;
}
