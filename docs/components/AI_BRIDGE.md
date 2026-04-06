# VernisOS — AI Bridge และ AI Engine

> ไฟล์ที่เกี่ยวข้อง: `kernel/drivers/ai_bridge.c`, `include/ai_bridge.h`, `ai/ai_listener.py`

---

## ภาพรวม

VernisOS มีระบบ AI แบบ 2 ชั้น:

```
┌─────────────────────────────────────────┐
│   Python AI Engine (Host / QEMU)        │  ← Phase 9–12
│   ai_listener.py                        │
│   ├── auto_tuner.py    (ปรับ scheduler) │
│   ├── anomaly_detector (ตรวจสอบ process)│
│   └── policy_manager   (อัปเดต policy) │
│              ↕ COM2 UART               │
├─────────────────────────────────────────┤
│   AI Bridge (kernel/drivers)            │  ← Phase 8
│   - ส่ง/รับ text frames ผ่าน COM2      │
│   - poll CMD ทุก 100ms (IRQ0)          │
├─────────────────────────────────────────┤
│   In-Kernel Rust AI Engine              │  ← Phase 10
│   - Event store                        │
│   - Anomaly detection rules            │
│   - Policy enforcement cache           │
│   - Access control table              │
└─────────────────────────────────────────┘
```

---

## COM2 UART Configuration

| Parameter | ค่า |
|-----------|-----|
| Port | `0x2F8` (COM2) |
| Baud rate | 115,200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 (8N1) |

### การตรวจสอบ COM2 Hardware

ตอน `ai_bridge_init()` ทดสอบว่ามี UART จริงหรือเปล่า:

```c
// เขียน 0xA5 ไปที่ Scratch Register (offset 7)
ai_outb(AI_COM_PORT + 7, 0xA5);
uint8_t scratch = ai_inb(AI_COM_PORT + 7);

if (scratch != 0xA5) {
    // COM2 ไม่มี — bus floating → คืนค่า 0xFF
    g_com2_hw = 0;
    g_status = AI_STATUS_OFFLINE;
    return;  // ไม่ทำอะไรเพิ่ม
}
g_com2_hw = 1;
```

ถ้าไม่มี COM2: ฟังก์ชันทั้งหมด (`ai_send_event`, `ai_poll_cmd`) จะ return ทันที — ไม่มีผลต่อ kernel

---

## โปรโตคอล COM2

ข้อความทุกชนิดเป็น **text line-delimited** (`\n` ท้าย):

### Kernel → Python

| ชนิด | รูปแบบ | ตัวอย่าง |
|------|--------|---------|
| REQ | `REQ\|<seq>\|<payload>\n` | `REQ\|1\|status\n` |
| EVT | `EVT\|<type>\|<data>\n` | `EVT\|BOOT\|version=0.1.0\n` |

### Python → Kernel

| ชนิด | รูปแบบ | ตัวอย่าง |
|------|--------|---------|
| RESP | `RESP\|<seq>\|<payload>\n` | `RESP\|1\|ok\n` |
| CMD TUNE | `CMD\|<seq>\|TUNE\|action\|target\|value\|reason\n` | `CMD\|5\|TUNE\|SCHED_QUANTUM\|*\|50\|high_load\n` |
| CMD REMEDIATE | `CMD\|<seq>\|REMEDIATE\|action\|target\|param\n` | `CMD\|6\|REMEDIATE\|kill\|3\|anomaly\n` |
| CMD POLICY | `CMD\|<seq>\|POLICY\|<hex-encoded VPOL>\n` | `CMD\|7\|POLICY\|56504F4C...\n` |

### Event Types

| Type | ความหมาย |
|------|----------|
| `BOOT` | Kernel เริ่มต้น |
| `PROC` | Process event (สร้าง/kill) |
| `MOD` | Module load/unload |
| `EXCP` | CPU exception |
| `DENY` | Command ถูกปฏิเสธ (sandbox/policy) |
| `FAIL` | Kernel error |
| `SYSCALL` | Syscall event |
| `STAT` | Periodic status report (ส่งทุก 1 วินาที) |

---

## Real-Time Polling ผ่าน IRQ0

```c
// ใน interrupt_dispatch(), vec == 0x20 (Timer IRQ)
kernel_tick++;
outb(0x20, 0x20);  // EOI ก่อน — เพื่อไม่บล็อก interrupt อื่น

if (kernel_tick % 10 == 0) {
    ai_poll_cmd();   // ทุก 10 ticks = 100ms: ตรวจ CMD จาก Python
}

if (kernel_tick % 100 == 0) {
    // ทุก 100 ticks = 1s: ส่ง STAT report
    void *sched = get_kernel_scheduler();
    uint32_t count = scheduler_get_process_count(sched);
    char buf[24];
    // สร้าง "process_count|N"
    ai_send_event("STAT", buf);
}
```

### ai_poll_cmd() — รับ Command

```c
void ai_poll_cmd(void) {
    if (!g_com2_hw) return;  // ออกเลยถ้าไม่มี UART

    // อ่านทีละ char จนเจอ '\n' หรือ timeout
    // parse format: CMD|seq|TUNE|action|target|value|reason
    // เรียก g_tune_handler(action, target, value, reason)
}
```

---

## API Functions

### Bridge Lifecycle

```c
void           ai_bridge_init(void);
AiBridgeStatus ai_bridge_status(void);  // ONLINE / OFFLINE / ERROR
void           ai_bridge_set_status(AiBridgeStatus s);
```

### ส่ง/รับ

```c
// ส่ง event (fire-and-forget ไม่รอ response)
void ai_send_event(const char *event_type, const char *data);

// Query แบบ synchronous (block ได้นาน ~5 วิ)
int ai_query_sync(const char *query, char *resp_buf, size_t max_len);

// เรียกจาก IRQ0 — ตรวจว่ามี CMD รอหรือเปล่า
void ai_poll_cmd(void);
```

### Handler Registration

```c
typedef void (*AiTuneHandler)(const char *action, const char *target,
                               uint32_t value, const char *reason);
typedef void (*AiRemediateHandler)(const char *action, const char *target,
                                    const char *param);

void ai_set_tune_handler(AiTuneHandler handler);
void ai_set_remediate_handler(AiRemediateHandler handler);
```

### Helper

```c
char *ai_build_event(char *buf, size_t max,
                     const char *part1, const char *part2, const char *part3);
```

---

## In-Kernel Rust AI Engine

Rust engine ทำงานใน kernel เอง ไม่ต้องพึ่ง COM2:

```c
// Initialize (ส่ง scheduler pointer ให้ engine monitor ได้)
void ai_kernel_engine_init(void *scheduler);

// Feed event เข้า engine
void ai_kernel_engine_feed(const char *event_type, const char *data, uint64_t now_ms);

// Tick — engine ประมวลผล queue (เรียกจาก idle loop)
void ai_kernel_engine_tick(uint64_t now_ms);

// Statistics
uint64_t ai_kernel_engine_event_count(void);
uint32_t ai_kernel_engine_anomaly_count(void);
uint32_t ai_kernel_engine_active_procs(void);

// Policy
uint32_t ai_kernel_engine_load_policy(const uint8_t *blob, size_t len);
uint16_t ai_kernel_engine_policy_version(void);
uint8_t  ai_kernel_engine_check_access(const char *command, size_t len);
```

---

## Python AI Engine (Host)

### รัน AI Listener กับ QEMU

```bash
# Terminal 1: Start Python listener
python3 ai/ai_listener.py --port 4444

# Terminal 2: Run VernisOS with COM2 bridged to TCP 4444
make run64-ai
```

QEMU ส่ง COM2 ผ่าน TCP:
```makefile
-chardev socket,id=ai,host=localhost,port=4444,server=off \
-device isa-serial,chardev=ai,index=1
```

### โมดูล Python

| โมดูล | หน้าที่ |
|-------|--------|
| `ai_listener.py` | Main loop รับ/ส่ง frame, dispatch ไปแต่ละ module |
| `auto_tuner.py` | วิเคราะห์ STAT events → ส่ง TUNE CMD กลับ kernel |
| `anomaly_detector.py` | ตรวจ process behavior ตาม rules ใน `anomaly_rules.yaml` |
| `policy_manager.py` | อัปเดต policy ใน kernel ผ่าน POLICY CMD |
| `event_store.py` | เก็บ event history ใน memory |
| `process_tracker.py` | ติดตาม process lifecycle จาก PROC events |

### Auto-Tuner Logic

```python
# เมื่อได้รับ STAT event: "process_count|N"
if process_count > HIGH_LOAD_THRESHOLD:
    send_cmd("TUNE|SCHED_QUANTUM|*|30|reduce_quantum_high_load")
elif process_count < LOW_LOAD_THRESHOLD:
    send_cmd("TUNE|SCHED_QUANTUM|*|100|increase_quantum_low_load")
```

---

## Syscall Interface

| Syscall | Number | คำอธิบาย |
|---------|--------|----------|
| `SYS_AI_QUERY` | 40 | Query AI engine synchronously |
| `SYS_AI_STATUS` | 41 | Get AI bridge status |
| `SYS_AI_EVENT` | 42 | Send custom event |

```c
// ตัวอย่าง: query AI จาก userspace (ในอนาคต)
int64_t result = syscall(40, (uint64_t)"analyze process 3", 0, 0);
```
