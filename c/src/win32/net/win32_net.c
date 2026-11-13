
global bool net_win32_wsa_started;

internal bool net_win32_startup(void) {
    if(net_win32_wsa_started) return true;
    WSADATA data;
    if(WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    net_win32_wsa_started = true;
    return true;
}

internal void net_win32_list_push(NET_WIN32_OpList *list, NET_WIN32_Op *op) {
    op->next = nullptr;
    op->prev = list->last;
    if(list->last) list->last->next = op;
    else list->first = op;
    list->last = op;
}

internal void net_win32_list_remove(NET_WIN32_OpList *list, NET_WIN32_Op *op) {
    if(op->prev) op->prev->next = op->next;
    else list->first = op->next;
    if(op->next) op->next->prev = op->prev;
    else list->last = op->prev;
    op->next = nullptr;
    op->prev = nullptr;
}

internal NET_WIN32_Op *net_win32_op_alloc(NET_Loop *loop, NET_OpKind kind, SOCKET socket, u64 deadline_us, void *user) {
    NET_WIN32_Op *op = loop->first_free;
    if(op) loop->first_free = op->next;
    else op = push_array_no_zero(loop->arena, NET_WIN32_Op, 1);
    MemoryZero(op, sizeof(*op));
    op->kind = kind;
    op->socket = socket;
    op->deadline_us = deadline_us;
    op->user = user;
    net_win32_list_push(&loop->pending, op);
    return op;
}

internal void net_win32_op_free(NET_Loop *loop, NET_WIN32_Op *op) {
    net_win32_list_remove(&loop->pending, op);
    op->next = loop->first_free;
    loop->first_free = op;
}

internal void net_win32_op_finish(NET_Loop *loop, NET_WIN32_Op *op, bool ok, u64 size) {
    op->pending_io = false;
    if(op->drop) {
        net_win32_op_free(loop, op);
        return;
    }
    NET_Socket socket = {0};
    if(op->kind == NET_OpKind_Dial && ok && !op->timed_out) socket.u64[0] = (u64)op->socket;
    else if(op->kind == NET_OpKind_Dial) { closesocket(op->socket); op->socket = INVALID_SOCKET; }
    NET_Result result = op->timed_out ? NET_Result_Timeout : (ok ? NET_Result_Ok : NET_Result_Failed);
    op->completion = (NET_Completion){.kind=op->kind, .result=result, .user=op->user, .socket=socket, .size=size};
    net_win32_list_remove(&loop->pending, op);
    net_win32_list_push(&loop->finished, op);
    op->finished = true;
}

internal bool net_win32_post(NET_Loop *loop, NET_WIN32_Op *op) {
    DWORD bytes = 0;
    int result;
    if(op->kind == NET_OpKind_Dial) {
        result = loop->connect_ex(op->socket, (struct sockaddr *)&op->address, op->address_size,
                                  nullptr, 0, &bytes, &op->overlapped);
    } else if(op->kind == NET_OpKind_Send) {
        u64 remaining = op->data.size - op->sent;
        WSABUF buffer = {.buf=(CHAR *)(op->data.str + op->sent), .len=(ULONG)Min(remaining, (u64)ULONG_MAX)};
        result = WSASend(op->socket, &buffer, 1, &bytes, 0, &op->overlapped, nullptr);
    } else {
        WSABUF buffer = {.buf=(CHAR *)op->buffer, .len=(ULONG)Min(op->capacity, (u64)ULONG_MAX)};
        DWORD flags = 0;
        result = WSARecv(op->socket, &buffer, 1, &bytes, &flags, &op->overlapped, nullptr);
    }
    if(result == 0 || WSAGetLastError() == WSA_IO_PENDING) {
        op->pending_io = true;
        return true;
    }
    net_win32_op_finish(loop, op, false, 0);
    return false;
}

internal void net_win32_completion(NET_Loop *loop, DWORD bytes, OVERLAPPED *overlapped, DWORD error) {
    NET_WIN32_Op *op = CONTAINING_RECORD(overlapped, NET_WIN32_Op, overlapped);
    op->pending_io = false;
    if(op->drop) {
        net_win32_op_free(loop, op);
        return;
    }
    if(error != ERROR_SUCCESS || op->timed_out) {
        net_win32_op_finish(loop, op, false, 0);
        return;
    }
    if(op->kind == NET_OpKind_Dial) {
        if(setsockopt(op->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) == SOCKET_ERROR) {
            net_win32_op_finish(loop, op, false, 0);
        } else {
            net_win32_op_finish(loop, op, true, 0);
        }
    } else if(op->kind == NET_OpKind_Send) {
        if(bytes == 0 && op->sent < op->data.size) {
            net_win32_op_finish(loop, op, false, 0);
            return;
        }
        op->sent += (u64)bytes;
        if(op->sent == op->data.size) net_win32_op_finish(loop, op, true, op->sent);
        else {
            MemoryZero(&op->overlapped, sizeof(op->overlapped));
            net_win32_post(loop, op);
        }
    } else {
        net_win32_op_finish(loop, op, true, (u64)bytes);
    }
}

internal void net_win32_dispatch_one(NET_Loop *loop, DWORD timeout_ms) {
    DWORD bytes = 0;
    ULONG_PTR key = 0;
    OVERLAPPED *overlapped = nullptr;
    BOOL ok = GetQueuedCompletionStatus(loop->iocp, &bytes, &key, &overlapped, timeout_ms);
    (void)key;
    if(overlapped) {
        DWORD error = ok ? ERROR_SUCCESS : GetLastError();
        net_win32_completion(loop, bytes, overlapped, error);
    }
}

internal struct sockaddr_storage net_win32_sockaddr(NET_Address address, int *size) {
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
    if(!net_win32_startup()) return false;
    Temp scratch = scratch_begin(nullptr, 0);
    String8 text = push_str8_copy(scratch.arena, ip);
    NET_Address address = {.is_ip6=is_ip6, .port=port};
    bool ok = InetPtonA(is_ip6 ? AF_INET6 : AF_INET, (char *)text.str, address.ip) == 1;
    scratch_end(scratch);
    if(ok) *out = address;
    return ok;
}

internal NET_Loop *net_loop_alloc(Arena *arena) {
    AssertAlways(net_win32_startup());
    NET_Loop *loop = push_array(arena, NET_Loop, 1);
    loop->arena = arena;
    loop->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    AssertAlways(loop->iocp != nullptr);
    SOCKET socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    AssertAlways(socket != INVALID_SOCKET);
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    AssertAlways(WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                          &loop->connect_ex, sizeof(loop->connect_ex), &bytes, nullptr, nullptr) == 0);
    closesocket(socket);
    return loop;
}

internal void net_loop_release(NET_Loop *loop) {
    CloseHandle(loop->iocp);
    if(net_win32_wsa_started) {
        WSACleanup();
        net_win32_wsa_started = false;
    }
}

internal void net_dial(NET_Loop *loop, NET_Address address, u64 deadline_us, void *user) {
    int address_size = 0;
    struct sockaddr_storage storage = net_win32_sockaddr(address, &address_size);
    SOCKET socket = WSASocketW(((struct sockaddr *)&storage)->sa_family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    NET_WIN32_Op *op = net_win32_op_alloc(loop, NET_OpKind_Dial, socket, deadline_us, user);
    if(socket == INVALID_SOCKET || !CreateIoCompletionPort((HANDLE)socket, loop->iocp, 0, 0)) {
        net_win32_op_finish(loop, op, false, 0);
        return;
    }
    op->address = storage;
    op->address_size = address_size;
    if(address.is_ip6) {
        struct sockaddr_in6 local = {.sin6_family=AF_INET6};
        if(bind(socket, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) { net_win32_op_finish(loop, op, false, 0); return; }
    } else {
        struct sockaddr_in local = {.sin_family=AF_INET};
        if(bind(socket, (struct sockaddr *)&local, sizeof(local)) == SOCKET_ERROR) { net_win32_op_finish(loop, op, false, 0); return; }
    }
    net_win32_post(loop, op);
}

internal void net_send(NET_Loop *loop, NET_Socket socket, String8 data, u64 deadline_us, void *user) {
    NET_WIN32_Op *op = net_win32_op_alloc(loop, NET_OpKind_Send, (SOCKET)socket.u64[0], deadline_us, user);
    op->data = data;
    net_win32_post(loop, op);
}

internal void net_recv(NET_Loop *loop, NET_Socket socket, u8 *buffer, u64 capacity, u64 deadline_us, void *user) {
    NET_WIN32_Op *op = net_win32_op_alloc(loop, NET_OpKind_Recv, (SOCKET)socket.u64[0], deadline_us, user);
    op->buffer = buffer;
    op->capacity = capacity;
    net_win32_post(loop, op);
}

internal void net_close(NET_Loop *loop, NET_Socket socket) {
    SOCKET fd = (SOCKET)socket.u64[0];
    for(NET_WIN32_Op *op = loop->pending.first; op; op = op->next) {
        if(op->socket == fd) { op->drop = true; CancelIoEx((HANDLE)fd, &op->overlapped); }
    }
    for(;;) {
        bool found = false;
        for(NET_WIN32_Op *op = loop->pending.first; op; op = op->next) if(op->socket == fd) found = true;
        if(!found) break;
        net_win32_dispatch_one(loop, INFINITE);
    }
    for(NET_WIN32_Op *op = loop->finished.first, *next = nullptr; op; op = next) {
        next = op->next;
        if(op->socket == fd) {
            net_win32_list_remove(&loop->finished, op);
            op->next = loop->first_free;
            loop->first_free = op;
        }
    }
    closesocket(fd);
}

internal u64 net_wait(NET_Loop *loop, u64 wake_at_us, NET_Completion *completions, u64 capacity) {
    if(!loop->finished.first) {
        u64 until = wake_at_us;
        for(NET_WIN32_Op *op = loop->pending.first; op; op = op->next) until = Min(until, op->deadline_us);
        u64 now = os_now_us();
        DWORD timeout = INFINITE;
        if(until != UINT64_MAX) {
            u64 wait_us = until > now ? until - now : 0;
            u64 wait_ms = (wait_us + 999) / 1000;
            timeout = wait_ms >= INFINITE ? INFINITE - 1 : (DWORD)wait_ms;
        }
        net_win32_dispatch_one(loop, timeout);
        now = os_now_us();
        for(NET_WIN32_Op *op = loop->pending.first; op; op = op->next) {
            if(op->deadline_us <= now && !op->timed_out) {
                op->timed_out = true;
                if(!CancelIoEx((HANDLE)op->socket, &op->overlapped) && GetLastError() == ERROR_NOT_FOUND) {
                }
            }
        }
        if(!loop->finished.first) {
            bool canceling = false;
            for(NET_WIN32_Op *op = loop->pending.first; op; op = op->next) if(op->timed_out) canceling = true;
            if(canceling) {
                do { net_win32_dispatch_one(loop, INFINITE); }
                while(!loop->finished.first && loop->pending.first);
            }
        }
    }
    u64 count = 0;
    while(loop->finished.first && count < capacity) {
        NET_WIN32_Op *op = loop->finished.first;
        net_win32_list_remove(&loop->finished, op);
        completions[count++] = op->completion;
        op->next = loop->first_free;
        loop->first_free = op;
    }
    return count;
}
