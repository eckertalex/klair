#ifndef MAC_NET_H
#define MAC_NET_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct NET_MAC_Op NET_MAC_Op;
struct NET_MAC_Op {
    NET_MAC_Op *next;
    NET_MAC_Op *prev;

    NET_OpKind kind;
    int fd;
    i16 filter; // the kqueue filter it's registered for, 0 when it isn't
    void *user;
    u64 deadline_us;

    String8 data; // Send
    u64 sent;
    u8 *buffer;   // Recv
    u64 capacity;

    NET_Completion completion;
};

typedef struct NET_MAC_OpList NET_MAC_OpList;
struct NET_MAC_OpList {
    NET_MAC_Op *first;
    NET_MAC_Op *last;
};

struct NET_Loop {
    Arena *arena;
    int kq;
    NET_MAC_OpList pending;  // registered with kqueue
    NET_MAC_OpList finished; // done, handed out by the next net_wait
    NET_MAC_Op *first_free;
};

#endif // MAC_NET_H
