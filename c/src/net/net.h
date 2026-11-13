#ifndef NET_H
#define NET_H


typedef struct NET_Socket NET_Socket;
struct NET_Socket {
    u64 u64[1];
};

typedef struct NET_Address NET_Address;
struct NET_Address {
    u8 ip[16]; // 4 bytes used for IPv4
    bool is_ip6;
    u16 port;
};

typedef enum NET_OpKind {
    NET_OpKind_Dial,
    NET_OpKind_Send,
    NET_OpKind_Recv,
} NET_OpKind;

typedef enum NET_Result {
    NET_Result_Ok,
    NET_Result_Failed,
    NET_Result_Timeout, // the deadline passed first
} NET_Result;

typedef struct NET_Completion NET_Completion;
struct NET_Completion {
    NET_OpKind kind;
    NET_Result result;
    void *user;
    NET_Socket socket; // Dial: the connected socket
    u64 size;          // Send: bytes sent, always all of them. Recv: bytes received, 0 when the peer closed
};

typedef struct NET_Loop NET_Loop;

// A numeric address only, never a DNS lookup
internal bool net_address_from_str8(String8 ip, bool is_ip6, u16 port, NET_Address *out);

internal NET_Loop *net_loop_alloc(Arena *arena);
internal void net_loop_release(NET_Loop *loop);

internal void net_dial(NET_Loop *loop, NET_Address address, u64 deadline_us, void *user);
internal void net_send(NET_Loop *loop, NET_Socket socket, String8 data, u64 deadline_us, void *user);
internal void net_recv(NET_Loop *loop, NET_Socket socket, u8 *buffer, u64 capacity, u64 deadline_us, void *user);

internal void net_close(NET_Loop *loop, NET_Socket socket);

internal u64 net_wait(NET_Loop *loop, u64 wake_at_us, NET_Completion *completions, u64 capacity);

#endif // NET_H
