# Phase 4 — IPC (Inter-Process Communication)

> สัปดาห์ 9–10 | ภาษา: C | สถานะ: ✅

---

## เป้าหมาย

ออกแบบและ implement ระบบ IPC สำหรับ microkernel ที่รองรับสองกลไก: **Message Queue** (fixed-size 64-byte messages) และ **Channel** (byte-stream ring buffer) พร้อม syscall interface 8 entries (syscall 20–27) และ spinlock สำหรับ thread safety

---

## ภาพรวม

ใน microkernel ทุก service ที่ต้องการสื่อสารกันต้องใช้ IPC — ไม่มี shared memory โดยตรง IPC layer ของ VernisOS แบ่งเป็นสอง primitive:

```
┌─────────────┐         ┌─────────────┐
│  Process A  │         │  Process B  │
│  (PID = 1)  │         │  (PID = 2)  │
└──────┬──────┘         └──────┬──────┘
       │                       │
       │  ipc_send(2, msg)      │
       ▼                       ▼
┌──────────────────────────────────────┐
│            IPC Layer (C)             │
│                                      │
│  ┌────────────────────────────────┐  │
│  │  Message Queue  (fixed 64B)    │  │
│  │  g_queues[MAX_QUEUES=64]       │  │
│  │  g_pid_to_qid[256] — O(1) map  │  │
│  └────────────────────────────────┘  │
│                                      │
│  ┌────────────────────────────────┐  │
│  │  Channel  (byte-stream)        │  │
│  │  g_channels[MAX_CHANNELS=32]   │  │
│  │  ring buffer, peer_pid pair    │  │
│  └────────────────────────────────┘  │
└──────────────────────────────────────┘
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | หน้าที่ |
|------|------|---------|
| `kernel/ipc/ipc.c` | C | Implementation ทั้งหมด |
| `kernel/include/ipc.h` | C | Structs, constants, API declarations |
| `kernel/core/verniskernel/src/syscall.rs` | Rust | ลงทะเบียน syscall 20–27 |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. IpcMessage — Fixed 64-byte Message

```c
// kernel/include/ipc.h
#define IPC_MAX_PAYLOAD  52   // 64 - 12 bytes header

typedef struct {
    uint32_t src_pid;         // PID ผู้ส่ง
    uint32_t dst_pid;         // PID ผู้รับ
    uint16_t type;            // message type (user-defined)
    uint16_t len;             // ความยาว payload จริง (0–52)
    uint8_t  data[IPC_MAX_PAYLOAD];  // payload
} __attribute__((packed)) IpcMessage;
// sizeof(IpcMessage) = 64 bytes (cache-line friendly)
```

### 2. IpcQueue — Message Ring Buffer

```c
// kernel/include/ipc.h
#define IPC_QUEUE_DEPTH  16   // จุดเก็บ message ต่อ queue

typedef struct {
    IpcMessage  messages[IPC_QUEUE_DEPTH];
    uint32_t    owner_pid;
    uint8_t     head;         // index อ่านออก
    uint8_t     tail;         // index เขียนเข้า
    uint8_t     count;        // จำนวน message ที่รอ
    uint8_t     _pad;
    volatile int spinlock;    // 0=free, 1=locked
} IpcQueue;
```

**Ring Buffer Logic:**

```
head=0, tail=0, count=0 → empty
head=0, tail=3, count=3 → 3 messages pending

write: messages[tail] = msg; tail = (tail+1) % 16; count++
read:  msg = messages[head]; head = (head+1) % 16; count--
full:  count == IPC_QUEUE_DEPTH
```

### 3. IpcChannel — Byte-Stream Ring Buffer

```c
// kernel/include/ipc.h
#define IPC_CHAN_BUF_SIZE  1024   // 1 KB per channel

typedef struct {
    uint8_t  buf[IPC_CHAN_BUF_SIZE];
    uint32_t owner_pid;
    uint32_t peer_pid;
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint8_t  closed;          // 1 = channel ถูกปิด
    volatile int spinlock;
} IpcChannel;
```

### 4. O(1) PID → Queue Lookup

```c
// kernel/ipc/ipc.c
#define IPC_MAX_QUEUES    64
#define IPC_MAX_CHANNELS  32
#define IPC_NO_QUEUE     255

static IpcQueue   g_queues[IPC_MAX_QUEUES];
static IpcChannel g_channels[IPC_MAX_CHANNELS];
static uint8_t    g_pid_to_qid[256];  // index 0–255 = PID, value = queue id

void ipc_init(void) {
    memset(g_queues,    0, sizeof(g_queues));
    memset(g_channels,  0, sizeof(g_channels));
    memset(g_pid_to_qid, IPC_NO_QUEUE, sizeof(g_pid_to_qid));
}
```

**เหตุผลที่ใช้ array ขนาด 256:** PID ใน VernisOS จำกัดที่ 0–255 ทำให้ lookup เป็น O(1) โดยใช้ PID เป็น index โดยตรง — ไม่ต้องใช้ hash table

### 5. ipc_send / ipc_recv

```c
// kernel/ipc/ipc.c
int ipc_send(uint32_t dst_pid, const IpcMessage *msg) {
    if (dst_pid >= 256) return -EINVAL;

    uint8_t qid = g_pid_to_qid[dst_pid];
    if (qid == IPC_NO_QUEUE) return -ESRCH;

    IpcQueue *q = &g_queues[qid];

    // Spinlock acquire
    while (__atomic_test_and_set(&q->spinlock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");

    int ret = 0;
    if (q->count >= IPC_QUEUE_DEPTH) {
        ret = -EAGAIN;   // queue เต็ม — non-blocking
    } else {
        q->messages[q->tail] = *msg;
        q->tail  = (q->tail + 1) % IPC_QUEUE_DEPTH;
        q->count++;
    }

    __atomic_clear(&q->spinlock, __ATOMIC_RELEASE);
    return ret;
}

int ipc_recv(uint32_t pid, IpcMessage *out) {
    uint8_t qid = g_pid_to_qid[pid];
    if (qid == IPC_NO_QUEUE) return -ESRCH;

    IpcQueue *q = &g_queues[qid];
    while (__atomic_test_and_set(&q->spinlock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");

    int ret = 0;
    if (q->count == 0) {
        ret = -EAGAIN;   // ไม่มี message — non-blocking return
    } else {
        *out  = q->messages[q->head];
        q->head  = (q->head + 1) % IPC_QUEUE_DEPTH;
        q->count--;
    }

    __atomic_clear(&q->spinlock, __ATOMIC_RELEASE);
    return ret;
}
```

### 6. Channel Write / Read

```c
int ipc_channel_write(uint32_t chan_id, const uint8_t *data, uint32_t len) {
    if (chan_id >= IPC_MAX_CHANNELS) return -EINVAL;
    IpcChannel *ch = &g_channels[chan_id];

    while (__atomic_test_and_set(&ch->spinlock, __ATOMIC_ACQUIRE))
        __asm__ volatile("pause");

    int written = 0;
    while (written < (int)len &&
           ch->count < IPC_CHAN_BUF_SIZE) {
        ch->buf[ch->tail] = data[written++];
        ch->tail  = (ch->tail + 1) % IPC_CHAN_BUF_SIZE;
        ch->count++;
    }

    __atomic_clear(&ch->spinlock, __ATOMIC_RELEASE);
    return written;
}
```

---

## โครงสร้างข้อมูล / API หลัก

### IPC API

```c
// kernel/include/ipc.h — Public API
void ipc_init(void);

// Message Queue
int  ipc_queue_create(uint32_t owner_pid);
int  ipc_queue_close(uint32_t owner_pid);
int  ipc_send(uint32_t dst_pid, const IpcMessage *msg);
int  ipc_recv(uint32_t own_pid, IpcMessage *out);

// Channel
int  ipc_channel_create(uint32_t owner_pid, uint32_t peer_pid);
int  ipc_channel_write(uint32_t chan_id,
                       const uint8_t *data, uint32_t len);
int  ipc_channel_read(uint32_t chan_id,
                      uint8_t *buf, uint32_t max_len);
int  ipc_channel_close(uint32_t chan_id);

// Stats
typedef struct {
    uint64_t messages_sent;
    uint64_t messages_dropped;
    uint64_t channel_bytes_written;
    uint64_t channel_bytes_read;
} IpcStats;
extern IpcStats g_ipc_stats;
```

### Syscall Table (20–27)

| Syscall# | ชื่อ | พารามิเตอร์ | ผลลัพธ์ |
|----------|------|-----------|---------|
| 20 | `SYS_IPC_SEND` | dst_pid, msg_ptr | 0/-errno |
| 21 | `SYS_IPC_RECV` | own_pid, buf_ptr | 0/-errno |
| 22 | `SYS_IPC_QUEUE_CREATE` | owner_pid | qid/-errno |
| 23 | `SYS_IPC_QUEUE_CLOSE` | owner_pid | 0/-errno |
| 24 | `SYS_IPC_CHAN_CREATE` | owner_pid, peer_pid | chan_id/-errno |
| 25 | `SYS_IPC_CHAN_WRITE` | chan_id, buf, len | bytes/-errno |
| 26 | `SYS_IPC_CHAN_READ` | chan_id, buf, max | bytes/-errno |
| 27 | `SYS_IPC_CHAN_CLOSE` | chan_id | 0/-errno |

---

## ขั้นตอนการทำงาน

### การส่ง Message

```
Process A (PID=1)
  │  เรียก syscall 20: ipc_send(dst=2, msg)
  │
  ▼ kernel
  ipc_send(2, msg)
  │
  ├─ ตรวจ dst_pid=2 < 256 ✓
  ├─ qid = g_pid_to_qid[2]  → O(1) lookup
  ├─ spinlock acquire
  ├─ ตรวจ count < 16 ✓
  ├─ messages[tail] = msg
  ├─ tail = (tail+1) % 16
  ├─ count++
  └─ spinlock release → return 0

Process B (PID=2) — polling / timer interrupt
  │  เรียก syscall 21: ipc_recv(own=2, &out)
  │
  ▼ kernel
  ipc_recv(2, out)
  │
  ├─ qid = g_pid_to_qid[2]
  ├─ spinlock acquire
  ├─ count > 0 ✓
  ├─ *out = messages[head]
  ├─ head = (head+1) % 16
  ├─ count--
  └─ spinlock release → return 0
```

### การสร้าง Channel

```
1. Process A: syscall 24 → ipc_channel_create(owner=1, peer=2) → chan_id=0
2. Process A: syscall 25 → ipc_channel_write(0, data, 100) → 100 bytes
3. Process B: syscall 26 → ipc_channel_read(0, buf, 256) → 100 bytes
4. Process A: syscall 27 → ipc_channel_close(0)
5. Process B: ipc_channel_read(0) → -EBADF (closed)
```

---

## ผลลัพธ์

- Message Queue ทำงานถูกต้อง: ส่ง/รับ 64-byte message แบบ non-blocking
- Channel ทำงาน: byte stream ระหว่าง 2 process
- `g_pid_to_qid[256]` ทำให้ lookup O(1) — ไม่ต้อง linear scan
- Spinlock ป้องกัน race condition บน SMP (single-core ยังไม่ใช้ SMP แต่ป้องกันไว้)
- IpcStats นับสถิติทุก operation สำหรับ debugging
- ทดสอบบน QEMU: kernel process 0 ส่ง message ไป process 1 สำเร็จ

## ข้อจำกัดและหนี้ทางเทคนิค

| ข้อจำกัด | เหตุผล | แผนแก้ไข |
|----------|---------|----------|
| Non-blocking recv เท่านั้น | ยังไม่มี scheduler yield | Phase 6+ blocking sleep |
| ไม่มี pipe | ไม่ได้ scope ใน Phase 4 | Phase 7+ |
| Static pool (64 queue, 32 chan) | ง่ายกว่า dynamic alloc | Dynamic ใน Phase 9+ |
| PID จำกัด 0–255 | g_pid_to_qid array size | ขยาย Phase 6+ |

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 5 implement **Module Loader** ที่ใช้ Rust registry จัดการ loadable module — module แต่ละตัวสื่อสารผ่าน IPC ที่ implement ใน Phase 4 นี้ และต้องมี capability `CAP_MODULE_LOAD` จาก sandbox ก่อนโหลดได้
