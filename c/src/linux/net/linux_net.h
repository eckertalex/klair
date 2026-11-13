#ifndef LINUX_NET_H
#define LINUX_NET_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/io_uring.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

#define NET_LINUX_RING_ENTRIES 256

typedef struct NET_LINUX_Op NET_LINUX_Op;
struct NET_LINUX_Op {
    NET_LINUX_Op *next;
    NET_LINUX_Op *prev;
    NET_OpKind kind;
    int fd;
    void *user;
    u64 deadline_us;
    String8 data;
    u64 sent;
    u8 *buffer;
    u64 capacity;
    struct sockaddr_storage address;
    socklen_t address_size;
    NET_Completion completion;
    bool main_done;
    bool cancel_done;
    bool cancel_sent;
    bool timed_out;
    bool drop;
    bool finished;
    bool failed;
    i32 result;
};

typedef struct NET_LINUX_OpList NET_LINUX_OpList;
struct NET_LINUX_OpList {
    NET_LINUX_Op *first;
    NET_LINUX_Op *last;
};

struct NET_Loop {
    Arena *arena;
    int ring_fd;
    struct io_uring_params params;
    void *sq_ring;
    void *cq_ring;
    size_t sq_ring_size;
    size_t cq_ring_size;
    struct io_uring_sqe *sqes;
    size_t sqes_size;
    u32 *sq_head;
    u32 *sq_tail;
    u32 *sq_mask;
    u32 *sq_entries;
    u32 *sq_array;
    u32 *cq_head;
    u32 *cq_tail;
    u32 *cq_mask;
    struct io_uring_cqe *cqes;
    NET_LINUX_OpList pending;
    NET_LINUX_OpList finished;
    NET_LINUX_Op *first_free;
};

#endif // LINUX_NET_H
