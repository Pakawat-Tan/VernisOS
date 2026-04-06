// --- TCP checksum calculation stub ---
#include <stdint.h>

// Forward declarations for functions used before definition
void tcp_send_syn(int sock);
void tcp_send_synack(int sock);
void tcp_send_ack(int sock);
extern uint32_t get_kernel_tick(void);
uint16_t tcp_calc_checksum(const void *packet, int len, uint32_t src_ip, uint32_t dst_ip) {
    // TODO: Implement real checksum (RFC 793)
    // For now, return 0
    (void)packet; (void)len; (void)src_ip; (void)dst_ip;
    return 0;
}

#include "tcp.h"
// Kernel does not have <string.h>, use forward declaration
void *memset(void *s, int c, unsigned long n);

TcpControlBlock g_tcbs[TCP_MAX_SOCKETS];

void tcp_init() {
    memset(g_tcbs, 0, sizeof(g_tcbs));
    for (int i = 0; i < TCP_MAX_SOCKETS; ++i) {
        g_tcbs[i].state = TCP_CLOSED;
    }
}

int tcp_listen(uint16_t port) {
    // Find free TCB
    for (int i = 0; i < TCP_MAX_SOCKETS; ++i) {
        if (g_tcbs[i].state == TCP_CLOSED) {
            g_tcbs[i].state = TCP_LISTEN;
            g_tcbs[i].local_port = port;
            return i;
        }
    }
    return -1;
}

int tcp_connect(uint32_t ip, uint16_t port) {
    // Find free TCB
    for (int i = 0; i < TCP_MAX_SOCKETS; ++i) {
        if (g_tcbs[i].state == TCP_CLOSED) {
            g_tcbs[i].state = TCP_SYN_SENT;
            g_tcbs[i].remote_ip = ip;
            g_tcbs[i].remote_port = port;
            // Send SYN packet
            tcp_send_syn(i);
            return i;
        }
    }
    return -1;
}

int tcp_send(int sock, const void *buf, int len) {
    // TODO: implement send
    (void)sock; (void)buf; (void)len;
    return -1;
}

int tcp_recv(int sock, void *buf, int maxlen) {
    // TODO: implement recv
    (void)sock; (void)buf; (void)maxlen;
    return -1;
}

int tcp_close(int sock) {
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return -1;
    g_tcbs[sock].state = TCP_CLOSED;
    return 0;
}

void tcp_tick() {
    // Basic retransmission timer for handshake
    uint32_t now = get_kernel_tick();
    for (int i = 0; i < TCP_MAX_SOCKETS; ++i) {
        TcpControlBlock *tcb = &g_tcbs[i];
        if (tcb->state == TCP_SYN_SENT || tcb->state == TCP_SYN_RECEIVED) {
            if (now - tcb->last_activity_tick > 120) { // ~0.5s at 240Hz
                if (tcb->retransmit_count < 5) {
                    // Retransmit SYN or SYN+ACK
                    if (tcb->state == TCP_SYN_SENT) tcp_send_syn(i);
                    else tcp_send_synack(i);
                    tcb->retransmit_count++;
                    tcb->last_activity_tick = now;
                } else {
                    // Give up, close socket
                    tcb->state = TCP_CLOSED;
                }
            }
        }
    }
}


// --- TCP packet dispatch stub (to be called from network layer) ---
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
    // ...payload follows
} __attribute__((packed)) TcpHeader;

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10
#define TCP_FLAG_URG 0x20

// Entry point for incoming TCP packet (to be called from network layer)
void tcp_receive_packet(uint32_t src_ip, uint32_t dst_ip, const void *packet, int len) {
    if (len < (int)sizeof(TcpHeader)) return;
    const TcpHeader *hdr = (const TcpHeader*)packet;
    // Find matching TCB by local/remote port/ip
    for (int i = 0; i < TCP_MAX_SOCKETS; ++i) {
        TcpControlBlock *tcb = &g_tcbs[i];
        if (tcb->state == TCP_LISTEN && tcb->local_port == hdr->dst_port) {
            // Passive open: SYN received
            if (hdr->flags & TCP_FLAG_SYN) {
                tcb->state = TCP_SYN_RECEIVED;
                tcb->remote_ip = src_ip;
                tcb->remote_port = hdr->src_port;
                tcb->seq_num = 0; // TODO: pick ISN
                tcb->ack_num = hdr->seq_num + 1;
                tcp_send_synack(i);
            }
        } else if (tcb->state == TCP_SYN_SENT && tcb->remote_ip == src_ip && tcb->remote_port == hdr->src_port) {
            // Active open: SYN-ACK received
            if ((hdr->flags & (TCP_FLAG_SYN|TCP_FLAG_ACK)) == (TCP_FLAG_SYN|TCP_FLAG_ACK)) {
                tcb->state = TCP_ESTABLISHED;
                tcb->ack_num = hdr->seq_num + 1;
                tcp_send_ack(i);
            }
        } else if (tcb->state == TCP_SYN_RECEIVED && tcb->remote_ip == src_ip && tcb->remote_port == hdr->src_port) {
            // Passive open: ACK received
            if (hdr->flags & TCP_FLAG_ACK) {
                tcb->state = TCP_ESTABLISHED;
            }
        }
        // ...other state transitions...
    }
}

// --- TCP packet send stubs ---
void tcp_send_syn(int sock) {
    // Example: Build and send SYN packet
    TcpControlBlock *tcb = &g_tcbs[sock];
    // Pick Initial Sequence Number (ISN)
    tcb->seq_num = 1000 + sock * 100; // TODO: use better ISN (timer-based)
    tcb->ack_num = 0;
    tcb->window = 4096;
    // TODO: Build TCPHeader and call network_send_tcp(...)
}

void tcp_send_synack(int sock) {
    TcpControlBlock *tcb = &g_tcbs[sock];
    // Pick ISN for passive open
    tcb->seq_num = 2000 + sock * 100; // TODO: use better ISN
    tcb->window = 4096;
    // TODO: Build TCPHeader with SYN+ACK, ack_num from received SYN
    // TODO: call network_send_tcp(...)
}

void tcp_send_ack(int sock) {
    TcpControlBlock *tcb = &g_tcbs[sock];
    // ACK: seq_num already set, ack_num updated from received packet
    // TODO: Build TCPHeader with ACK, call network_send_tcp(...)
}
