# VernisOS — IPC (การสื่อสารระหว่าง Process)

> ไฟล์ที่เกี่ยวข้อง: `kernel/ipc/ipc.c`, `include/ipc.h`

---

## ภาพรวม

VernisOS IPC มี 2 กลไก:

| กลไก | ลักษณะ | ใช้เมื่อ |
|------|--------|---------|
| **Message Queue** | ข้อความ fixed-size 64 bytes | ส่งคำสั่ง/notification สั้นๆ |
| **Channel** | byte-stream ring buffer | ส่งข้อมูลต่อเนื่อง (เช่น log, output) |

ทั้ง 2 กลไกเข้าถึงได้ผ่าน **syscall** (number 20–27) จาก process ใดก็ได้

---

## Message Queue

### โครงสร้างข้อมูล

```c
// ข้อความเดียว — 64 bytes
typedef struct {
    uint32_t src_pid;
    uint32_t dst_pid;
    uint32_t type;         // ชนิดข้อความ (กำหนดเองได้)
    uint32_t len;          // ขนาด payload จริง (max IPC_MSG_PAYLOAD)
    uint8_t  data[IPC_MSG_PAYLOAD];  // payload bytes
} IpcMessage;

// Queue ของ process
typedef struct {
    IpcMessage messages[IPC_QUEUE_DEPTH];  // ring buffer
    uint32_t   owner_pid;
    uint32_t   head, tail, count;
} IpcQueue;
```

**ขนาด constants:**
- `IPC_MSG_PAYLOAD` = payload ต่อ message
- `IPC_QUEUE_DEPTH` = ความลึก queue ต่อ process
- `IPC_MAX_QUEUES` = จำนวน queue สูงสุด

### PID-to-Queue Cache

```c
// O(1) lookup สำหรับ PID < 256
uint8_t g_pid_to_qid[256];
```

### ส่ง Message

```c
int ipc_send(uint32_t src_pid, uint32_t dst_pid,
             uint32_t type, const void *data, size_t len);
```

- ถ้า destination ยังไม่มี queue → สร้างอัตโนมัติ
- ถ้า queue เต็ม → `g_stats.messages_dropped++` แล้วคืน error
- thread-safe ผ่าน spinlock

### รับ Message

```c
int ipc_recv(uint32_t qid, IpcMessage *msg);
```

- คืนค่า `0` ถ้าได้ message, `-1` ถ้า queue ว่าง (non-blocking)

---

## Channel (Byte-Stream)

### โครงสร้างข้อมูล

```c
typedef struct {
    uint8_t  buf[IPC_CHAN_BUF_SIZE];  // ring buffer
    uint32_t owner_pid;
    uint32_t peer_pid;
    uint32_t head, tail, count;
    uint8_t  closed;   // ถ้า 1 → ไม่รับข้อมูลใหม่
} IpcChannel;
```

### Write/Read

```c
// เขียนข้อมูลเข้า channel (ส่งจาก sender)
int ipc_channel_write(uint32_t cid, const void *buf, size_t len);

// อ่านข้อมูลจาก channel (รับที่ receiver)
int ipc_channel_read(uint32_t cid, void *buf, size_t max_len);
```

---

## Syscall API

| Syscall | Number | Signature | คำอธิบาย |
|---------|--------|-----------|----------|
| `SYS_IPC_SEND` | 20 | `send(src, dst, type, data_ptr, len)` | ส่ง message |
| `SYS_IPC_RECV` | 21 | `recv(qid, msg_ptr)` | รับ message |
| `SYS_IPC_QUEUE_CREATE` | 22 | `queue_create(pid)` → qid | สร้าง queue |
| `SYS_IPC_QUEUE_CLOSE` | 23 | `queue_close(qid)` | ลบ queue |
| `SYS_IPC_CHAN_CREATE` | 24 | `chan_create(owner, peer)` → cid | สร้าง channel |
| `SYS_IPC_CHAN_WRITE` | 25 | `chan_write(cid, buf, len)` | เขียน channel |
| `SYS_IPC_CHAN_READ` | 26 | `chan_read(cid, buf, max)` | อ่าน channel |
| `SYS_IPC_CHAN_CLOSE` | 27 | `chan_close(cid)` | ปิด channel |

### ตัวอย่างการใช้งาน (จาก C kernel code)

```c
// สร้าง queue สำหรับ process 3
uint32_t qid = ipc_queue_create(3);

// ส่ง message หา process 3
const char *payload = "hello";
ipc_send(1, 3, MSG_TYPE_NOTIFY, payload, strlen(payload));

// process 3 รับ message
IpcMessage msg;
if (ipc_recv(qid, &msg) == 0) {
    // msg.data มีข้อมูล
    // msg.len = ขนาด
    // msg.src_pid = ใครส่ง
}
```

---

## Statistics

```c
typedef struct {
    uint64_t messages_sent;
    uint64_t messages_dropped;  // queue เต็ม
    uint32_t queues_active;
    uint32_t channels_active;
} IpcStats;

// ดูสถิติ
IpcStats stats = ipc_get_stats();
```

---

## ข้อจำกัดและแผนพัฒนา

| ข้อจำกัด | รายละเอียด |
|---------|-----------|
| Non-blocking เท่านั้น | `ipc_recv` ไม่ block — process ต้อง poll เอง |
| ไม่มี pipe (`\|`) | Shell ยังไม่รองรับ pipe ระหว่าง command |
| ไม่มี Unix socket | ไม่มี socket abstraction |
| Static pool | Queue และ Channel เป็น static array ไม่ dynamic |

**แผน Phase ต่อไป:**
- เพิ่ม blocking `ipc_recv` (process เข้า Waiting state รอ message)
- เพิ่ม pipe support ใน CLI (`cmd1 | cmd2`)
