#ifndef VERNISOS_IPC_H
#define VERNISOS_IPC_H

#include <stdint.h>
#include <stddef.h>

// =============================================================================
// IPC Constants
// =============================================================================

#define IPC_MAX_QUEUES    16    // max concurrent message queues
#define IPC_QUEUE_DEPTH   16    // messages per queue (ring buffer)
#define IPC_MSG_PAYLOAD   48    // bytes of payload per message
#define IPC_MAX_CHANNELS   8    // max concurrent channels
#define IPC_CHAN_BUF_SIZE 512   // channel ring buffer size (bytes)
#define IPC_MAX_USOCKETS    8   // max unix-like local sockets
#define IPC_USOCK_PATH_MAX 32

// Message types
#define IPC_TYPE_DATA     0x01  // raw data transfer
#define IPC_TYPE_SIGNAL   0x02  // signal / event notification
#define IPC_TYPE_CTRL     0x03  // control / command
#define IPC_TYPE_AI       0x10  // reserved for AI IPC bridge (Phase 8)

// Return codes
#define IPC_OK            0
#define IPC_ERR_FULL    (-1)    // queue or channel is full
#define IPC_ERR_EMPTY   (-2)    // queue or channel is empty
#define IPC_ERR_INVAL   (-3)    // invalid argument or id
#define IPC_ERR_NOSLOT  (-4)    // no free slot available

// IPC syscall numbers (20-27, handled in C before Rust syscall_handler)
#define SYS_IPC_SEND          20
#define SYS_IPC_RECV          21
#define SYS_IPC_QUEUE_CREATE  22
#define SYS_IPC_QUEUE_CLOSE   23
#define SYS_IPC_CHAN_CREATE    24
#define SYS_IPC_CHAN_WRITE     25
#define SYS_IPC_CHAN_READ      26
#define SYS_IPC_CHAN_CLOSE     27

// =============================================================================
// IPC Message — 64 bytes, cache-line friendly
// =============================================================================
typedef struct {
    uint32_t src_pid;               // sender PID
    uint32_t dst_pid;               // target PID  (0 = broadcast)
    uint32_t type;                  // IPC_TYPE_*
    uint32_t len;                   // payload bytes used (0..IPC_MSG_PAYLOAD)
    uint8_t  data[IPC_MSG_PAYLOAD]; // payload
} __attribute__((packed)) IpcMessage;  // 4+4+4+4+48 = 64 bytes

// =============================================================================
// Message Queue — per-process inbox
// =============================================================================
typedef struct {
    uint32_t   owner_pid;               // 0 = slot is free
    uint32_t   head, tail, count;
    IpcMessage msgs[IPC_QUEUE_DEPTH];
} IpcQueue;

// =============================================================================
// Channel — unidirectional byte stream
// Foundation for AI IPC bridge in Phase 8
// =============================================================================
typedef struct {
    uint32_t owner_pid;             // write-end PID
    uint32_t peer_pid;              // read-end PID  (0 = any)
    uint8_t  buf[IPC_CHAN_BUF_SIZE];
    uint16_t head, tail, count;
    uint8_t  closed;                // 1 after ipc_channel_close()
} IpcChannel;

// =============================================================================
// IPC Stats — returned to kernel for diagnostics
// =============================================================================
typedef struct {
    uint32_t queues_active;
    uint32_t channels_active;
    uint32_t messages_sent;
    uint32_t messages_dropped;
} IpcStats;

// =============================================================================
// Public API
// =============================================================================

void    ipc_init(void);
void    ipc_get_stats(IpcStats *out);

// Message Queue
int32_t ipc_queue_create(uint32_t pid);
void    ipc_queue_destroy(uint32_t qid);
int32_t ipc_send(uint32_t src_pid, uint32_t dst_pid,
                 uint32_t type, const void *data, uint32_t len);
int32_t ipc_recv(uint32_t qid, IpcMessage *out);
int32_t ipc_queue_count(uint32_t qid);

// Channel
int32_t ipc_channel_create(uint32_t owner_pid, uint32_t peer_pid);
int32_t ipc_channel_write(uint32_t cid, const uint8_t *buf, uint32_t len);
int32_t ipc_channel_read(uint32_t cid, uint8_t *buf, uint32_t max_len);
void    ipc_channel_close(uint32_t cid);

// Unix-like local socket API (path-based, backed by channels)
int32_t ipc_usock_bind(const char *path, uint32_t owner_pid);
int32_t ipc_usock_connect(const char *path, uint32_t peer_pid);
int32_t ipc_usock_send(const char *path, const uint8_t *buf, uint32_t len);
int32_t ipc_usock_recv(const char *path, uint8_t *buf, uint32_t max_len);
int32_t ipc_usock_close(const char *path);

// Syscall dispatcher — called from interrupt_dispatch for syscalls 20-27
int32_t ipc_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3);

#endif // VERNISOS_IPC_H
