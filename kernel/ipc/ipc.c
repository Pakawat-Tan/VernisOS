// ipc.c — Kernel IPC: Message Queue + Channel
// Phase 4: Inter-process Communication
//
// Architecture:
//   Message Queue : fixed-size 64-byte messages in a per-process ring buffer
//   Channel       : unidirectional byte-stream ring buffer
//                   (will be used as AI IPC bridge in Phase 8)

#include "ipc.h"
#include <stddef.h>

// Forward declarations of kernel helpers from kernel_x86.c
extern void serial_print(const char *s);
extern void serial_print_hex(uint32_t val);
extern void serial_print_dec(uint32_t val);

// =============================================================================
// Global IPC state
// =============================================================================

static IpcQueue    g_queues[IPC_MAX_QUEUES];
static IpcChannel  g_channels[IPC_MAX_CHANNELS];
static IpcStats    g_stats;

typedef struct {
    uint8_t  used;
    uint32_t cid;
    uint32_t owner_pid;
    char     path[IPC_USOCK_PATH_MAX];
} IpcUnixSocket;

static IpcUnixSocket g_usocks[IPC_MAX_USOCKETS];

// PID-to-QueueID direct-mapped cache — O(1) lookup (Phase 15)
#define IPC_PID_CACHE_SIZE 256
static int16_t g_pid_to_qid[IPC_PID_CACHE_SIZE];

// Minimal memset / memcpy (no libc in kernel)
static void ipc_memset(void *dst, uint8_t val, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}
static void ipc_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static int ipc_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return (uint8_t)*a - (uint8_t)*b;
}

static void ipc_strncpy(char *dst, const char *src, uint32_t max) {
    uint32_t i = 0;
    if (!dst || max == 0) return;
    while (src && src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int32_t usock_find_by_path(const char *path) {
    if (!path || !path[0]) return -1;
    for (int i = 0; i < IPC_MAX_USOCKETS; i++) {
        if (g_usocks[i].used && ipc_strcmp(g_usocks[i].path, path) == 0) return i;
    }
    return -1;
}

// =============================================================================
// Init
// =============================================================================

void ipc_init(void) {
    ipc_memset(g_queues,   0, sizeof(g_queues));
    ipc_memset(g_channels, 0, sizeof(g_channels));
    ipc_memset(g_usocks,   0, sizeof(g_usocks));
    ipc_memset(&g_stats,   0, sizeof(g_stats));
    for (int i = 0; i < IPC_PID_CACHE_SIZE; i++) g_pid_to_qid[i] = -1;
    serial_print("[ipc] initialized: ");
    serial_print_dec(IPC_MAX_QUEUES);
    serial_print(" queues, ");
    serial_print_dec(IPC_MAX_CHANNELS);
    serial_print(" channels\n");
}

void ipc_get_stats(IpcStats *out) {
    if (!out) return;
    ipc_memcpy(out, &g_stats, sizeof(IpcStats));
}

// =============================================================================
// Message Queue
// =============================================================================

// Find queue owned by pid — O(1) via cache, fallback to linear scan
static int32_t queue_find_by_pid(uint32_t pid) {
    if (pid < IPC_PID_CACHE_SIZE) {
        int16_t qid = g_pid_to_qid[pid];
        if (qid >= 0 && qid < IPC_MAX_QUEUES && g_queues[qid].owner_pid == pid)
            return qid;
    }
    // Fallback linear scan (for PIDs >= 256 or stale cache)
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (g_queues[i].owner_pid == pid) {
            if (pid < IPC_PID_CACHE_SIZE) g_pid_to_qid[pid] = (int16_t)i;
            return i;
        }
    }
    return -1;
}

// Allocate a new queue for pid; returns queue id (0-based) or IPC_ERR_NOSLOT
int32_t ipc_queue_create(uint32_t pid) {
    if (!pid) return IPC_ERR_INVAL;
    int32_t existing = queue_find_by_pid(pid);
    if (existing >= 0) return existing;
    for (int i = 0; i < IPC_MAX_QUEUES; i++) {
        if (g_queues[i].owner_pid == 0) {
            ipc_memset(&g_queues[i], 0, sizeof(IpcQueue));
            g_queues[i].owner_pid = pid;
            if (pid < IPC_PID_CACHE_SIZE) g_pid_to_qid[pid] = (int16_t)i;
            g_stats.queues_active++;
            return i;
        }
    }
    return IPC_ERR_NOSLOT;
}

void ipc_queue_destroy(uint32_t qid) {
    if (qid >= IPC_MAX_QUEUES) return;
    uint32_t pid = g_queues[qid].owner_pid;
    if (pid) {
        if (pid < IPC_PID_CACHE_SIZE) g_pid_to_qid[pid] = -1;
        ipc_memset(&g_queues[qid], 0, sizeof(IpcQueue));
        if (g_stats.queues_active) g_stats.queues_active--;
    }
}

// Send message to dst_pid's queue.
// Auto-creates queue for dst_pid if not yet present.
int32_t ipc_send(uint32_t src_pid, uint32_t dst_pid,
                 uint32_t type, const void *data, uint32_t len) {
    if (!dst_pid || !data) return IPC_ERR_INVAL;
    if (len > IPC_MSG_PAYLOAD) len = IPC_MSG_PAYLOAD;

    // Find or create queue for dst
    int32_t qid = queue_find_by_pid(dst_pid);
    if (qid < 0) qid = ipc_queue_create(dst_pid);
    if (qid < 0) {
        g_stats.messages_dropped++;
        return IPC_ERR_NOSLOT;
    }

    IpcQueue *q = &g_queues[qid];
    if (q->count >= IPC_QUEUE_DEPTH) {
        g_stats.messages_dropped++;
        return IPC_ERR_FULL;
    }

    IpcMessage *m = &q->msgs[q->tail];
    m->src_pid = src_pid;
    m->dst_pid = dst_pid;
    m->type    = type;
    m->len     = len;
    ipc_memset(m->data, 0, IPC_MSG_PAYLOAD);
    ipc_memcpy(m->data, data, len);

    q->tail = (q->tail + 1) % IPC_QUEUE_DEPTH;
    q->count++;
    g_stats.messages_sent++;
    return IPC_OK;
}

// Receive oldest message from queue qid into *out.
int32_t ipc_recv(uint32_t qid, IpcMessage *out) {
    if (qid >= IPC_MAX_QUEUES || !out) return IPC_ERR_INVAL;
    IpcQueue *q = &g_queues[qid];
    if (!q->owner_pid)  return IPC_ERR_INVAL;
    if (!q->count)      return IPC_ERR_EMPTY;

    ipc_memcpy(out, &q->msgs[q->head], sizeof(IpcMessage));
    q->head = (q->head + 1) % IPC_QUEUE_DEPTH;
    q->count--;
    return IPC_OK;
}

int32_t ipc_queue_count(uint32_t qid) {
    if (qid >= IPC_MAX_QUEUES) return IPC_ERR_INVAL;
    if (!g_queues[qid].owner_pid) return IPC_ERR_INVAL;
    return (int32_t)g_queues[qid].count;
}

// =============================================================================
// Channel
// =============================================================================

int32_t ipc_channel_create(uint32_t owner_pid, uint32_t peer_pid) {
    if (!owner_pid) return IPC_ERR_INVAL;
    for (int i = 0; i < IPC_MAX_CHANNELS; i++) {
        if (g_channels[i].owner_pid == 0) {
            ipc_memset(&g_channels[i], 0, sizeof(IpcChannel));
            g_channels[i].owner_pid = owner_pid;
            g_channels[i].peer_pid  = peer_pid;
            g_stats.channels_active++;
            return i;
        }
    }
    return IPC_ERR_NOSLOT;
}

int32_t ipc_channel_write(uint32_t cid, const uint8_t *buf, uint32_t len) {
    if (cid >= IPC_MAX_CHANNELS || !buf || !len) return IPC_ERR_INVAL;
    IpcChannel *ch = &g_channels[cid];
    if (!ch->owner_pid || ch->closed) return IPC_ERR_INVAL;

    uint32_t written = 0;
    while (written < len) {
        if (ch->count >= IPC_CHAN_BUF_SIZE) break;   // full — partial write
        ch->buf[ch->tail] = buf[written++];
        ch->tail = (uint16_t)((ch->tail + 1) % IPC_CHAN_BUF_SIZE);
        ch->count++;
    }
    return written ? (int32_t)written : IPC_ERR_FULL;
}

int32_t ipc_channel_read(uint32_t cid, uint8_t *buf, uint32_t max_len) {
    if (cid >= IPC_MAX_CHANNELS || !buf || !max_len) return IPC_ERR_INVAL;
    IpcChannel *ch = &g_channels[cid];
    if (!ch->owner_pid) return IPC_ERR_INVAL;
    if (!ch->count)     return IPC_ERR_EMPTY;

    uint32_t read = 0;
    while (read < max_len && ch->count > 0) {
        buf[read++] = ch->buf[ch->head];
        ch->head = (uint16_t)((ch->head + 1) % IPC_CHAN_BUF_SIZE);
        ch->count--;
    }
    return (int32_t)read;
}

void ipc_channel_close(uint32_t cid) {
    if (cid >= IPC_MAX_CHANNELS) return;
    if (g_channels[cid].owner_pid) {
        g_channels[cid].closed = 1;
        if (g_stats.channels_active) g_stats.channels_active--;
    }
}

// =============================================================================
// Unix-like local sockets (path-based)
// =============================================================================

int32_t ipc_usock_bind(const char *path, uint32_t owner_pid) {
    if (!path || !path[0] || !owner_pid) return IPC_ERR_INVAL;
    if (usock_find_by_path(path) >= 0) return IPC_ERR_INVAL;

    int32_t cid = ipc_channel_create(owner_pid, 0);
    if (cid < 0) return cid;

    for (int i = 0; i < IPC_MAX_USOCKETS; i++) {
        if (!g_usocks[i].used) {
            g_usocks[i].used = 1;
            g_usocks[i].cid = (uint32_t)cid;
            g_usocks[i].owner_pid = owner_pid;
            ipc_strncpy(g_usocks[i].path, path, IPC_USOCK_PATH_MAX);
            return IPC_OK;
        }
    }

    ipc_channel_close((uint32_t)cid);
    return IPC_ERR_NOSLOT;
}

int32_t ipc_usock_connect(const char *path, uint32_t peer_pid) {
    int32_t sid = usock_find_by_path(path);
    if (sid < 0) return IPC_ERR_INVAL;

    uint32_t cid = g_usocks[sid].cid;
    if (cid >= IPC_MAX_CHANNELS || !g_channels[cid].owner_pid) return IPC_ERR_INVAL;
    g_channels[cid].peer_pid = peer_pid;
    return (int32_t)cid;
}

int32_t ipc_usock_send(const char *path, const uint8_t *buf, uint32_t len) {
    int32_t sid = usock_find_by_path(path);
    if (sid < 0) return IPC_ERR_INVAL;
    return ipc_channel_write(g_usocks[sid].cid, buf, len);
}

int32_t ipc_usock_recv(const char *path, uint8_t *buf, uint32_t max_len) {
    int32_t sid = usock_find_by_path(path);
    if (sid < 0) return IPC_ERR_INVAL;
    return ipc_channel_read(g_usocks[sid].cid, buf, max_len);
}

int32_t ipc_usock_close(const char *path) {
    int32_t sid = usock_find_by_path(path);
    if (sid < 0) return IPC_ERR_INVAL;
    ipc_channel_close(g_usocks[sid].cid);
    ipc_memset(&g_usocks[sid], 0, sizeof(IpcUnixSocket));
    return IPC_OK;
}

// =============================================================================
// Syscall Dispatcher
// Syscall convention (x86 INT 0x80): EAX=num, EBX=a1, ECX=a2, EDX=a3
// =============================================================================

int32_t ipc_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    switch (num) {

    // SYS_IPC_SEND (20): a1=dst_pid, a2=ptr to IpcMessage
    case SYS_IPC_SEND: {
        if (!a2) return IPC_ERR_INVAL;
        const IpcMessage *m = (const IpcMessage *)a2;
        return ipc_send(m->src_pid, a1, m->type, m->data, m->len);
    }

    // SYS_IPC_RECV (21): a1=qid, a2=ptr to IpcMessage output buffer
    case SYS_IPC_RECV: {
        if (!a2) return IPC_ERR_INVAL;
        return ipc_recv(a1, (IpcMessage *)a2);
    }

    // SYS_IPC_QUEUE_CREATE (22): a1=pid → returns qid
    case SYS_IPC_QUEUE_CREATE:
        return ipc_queue_create(a1);

    // SYS_IPC_QUEUE_CLOSE (23): a1=qid
    case SYS_IPC_QUEUE_CLOSE:
        ipc_queue_destroy(a1);
        return IPC_OK;

    // SYS_IPC_CHAN_CREATE (24): a1=owner_pid, a2=peer_pid → returns cid
    case SYS_IPC_CHAN_CREATE:
        return ipc_channel_create(a1, a2);

    // SYS_IPC_CHAN_WRITE (25): a1=cid, a2=ptr, a3=len
    case SYS_IPC_CHAN_WRITE:
        if (!a2 || !a3) return IPC_ERR_INVAL;
        return ipc_channel_write(a1, (const uint8_t *)a2, a3);

    // SYS_IPC_CHAN_READ (26): a1=cid, a2=ptr, a3=max_len
    case SYS_IPC_CHAN_READ:
        if (!a2 || !a3) return IPC_ERR_INVAL;
        return ipc_channel_read(a1, (uint8_t *)a2, a3);

    // SYS_IPC_CHAN_CLOSE (27): a1=cid
    case SYS_IPC_CHAN_CLOSE:
        ipc_channel_close(a1);
        return IPC_OK;

    default:
        return IPC_ERR_INVAL;
    }
}
