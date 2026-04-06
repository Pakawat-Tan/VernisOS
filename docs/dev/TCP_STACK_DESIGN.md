# VernisOS TCP Stack Design (Phase 49)

## 1. TCP State Machine

- CLOSED
- LISTEN
- SYN_SENT
- SYN_RECEIVED
- ESTABLISHED
- FIN_WAIT_1
- FIN_WAIT_2
- CLOSE_WAIT
- CLOSING
- LAST_ACK
- TIME_WAIT

Transitions follow RFC 793 (SYN, SYN-ACK, ACK, FIN, RST, timeout).

## 2. Transmission Control Block (TCB)

```c
// include/tcp.h
#define TCP_MAX_SOCKETS 16

typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} TcpState;

typedef struct {
    TcpState state;
    uint32_t local_ip;
    uint16_t local_port;
    uint32_t remote_ip;
    uint16_t remote_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint16_t window;
    uint16_t remote_window;
    uint8_t  retransmit_count;
    uint32_t last_activity_tick;
    // Buffer pointers, flags, etc.
} TcpControlBlock;
```

## 3. API (Kernel)

```c
// include/tcp.h
void tcp_init();
int  tcp_listen(uint16_t port);
int  tcp_connect(uint32_t ip, uint16_t port);
int  tcp_send(int sock, const void *buf, int len);
int  tcp_recv(int sock, void *buf, int maxlen);
int  tcp_close(int sock);
void tcp_tick(); // Called from timer IRQ
```

## 4. Packet Flow

- RX: E1000 RX → eth_input() → ipv4_input() → tcp_input()
- TX: tcp_send() → ipv4_output() → eth_output() → E1000 TX

## 5. Retransmission

- Each TCB tracks retransmit_count, last_activity_tick
- tcp_tick() checks for timeout, triggers retransmit if needed

## 6. Checksum

- Implement TCP checksum (pseudo-header + TCP header + data)

## 7. Integration Points

- Kernel init: call tcp_init()
- Timer IRQ: call tcp_tick()
- CLI: add `tcpstat` command to show all sockets
- Future: connect to socket layer (Phase 50)

---

## Minimal File/Function Plan

- include/tcp.h — API, TCB struct, state enum
- kernel/net/tcp.c — Implementation
- kernel/arch/x86/kernel_x86.c — call tcp_init(), tcp_tick()
- kernel/arch/x86_64/kernel_x64.c — call tcp_init(), tcp_tick()
- kernel/shell/cli.c — add tcpstat command
