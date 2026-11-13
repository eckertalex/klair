#ifndef WIN32_NET_H
#define WIN32_NET_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <limits.h>

#pragma comment(lib, "Ws2_32.lib")

typedef struct NET_WIN32_Op NET_WIN32_Op;
struct NET_WIN32_Op {
    NET_WIN32_Op *next;
    NET_WIN32_Op *prev;
    OVERLAPPED overlapped;
    NET_OpKind kind;
    SOCKET socket;
    void *user;
    u64 deadline_us;
    String8 data;
    u64 sent;
    u8 *buffer;
    u64 capacity;
    struct sockaddr_storage address;
    int address_size;
    NET_Completion completion;
    bool pending_io;
    bool timed_out;
    bool drop;
    bool finished;
};

typedef struct NET_WIN32_OpList NET_WIN32_OpList;
struct NET_WIN32_OpList {
    NET_WIN32_Op *first;
    NET_WIN32_Op *last;
};

struct NET_Loop {
    Arena *arena;
    HANDLE iocp;
    LPFN_CONNECTEX connect_ex;
    NET_WIN32_OpList pending;
    NET_WIN32_OpList finished;
    NET_WIN32_Op *first_free;
};

#endif // WIN32_NET_H
