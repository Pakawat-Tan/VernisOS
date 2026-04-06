#ifndef VERNISOS_TCP_H
#define VERNISOS_TCP_H

#include <stdint.h>

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


typedef struct TcpControlBlock {
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

extern TcpControlBlock g_tcbs[TCP_MAX_SOCKETS];

void tcp_init();
int  tcp_listen(uint16_t port);
int  tcp_connect(uint32_t ip, uint16_t port);
int  tcp_send(int sock, const void *buf, int len);
int  tcp_recv(int sock, void *buf, int maxlen);
int  tcp_close(int sock);
void tcp_tick();

#endif // VERNISOS_TCP_H
