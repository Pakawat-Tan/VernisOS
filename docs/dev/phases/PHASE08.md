# Phase 8 — AI IPC Bridge (Kernel ↔ AI Engine)
> สัปดาห์ 19–21 | ภาษา: C (Kernel) + Python (Listener) | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

สร้างช่องทางสื่อสารสองทาง (**Bidirectional IPC Bridge**) ระหว่าง VernisOS Kernel และ Python AI Engine โดยใช้ **COM2 UART (0x2F8)** เป็น Transport Layer ผ่าน Protocol แบบ Text-based Frame ที่ออกแบบให้ parse ได้ง่ายจากทั้งสองฝั่ง

---

## ภาพรวม

```
┌─────────────────────────────┐         ┌──────────────────────────────┐
│       VernisOS Kernel       │         │      Python AI Engine        │
│                             │         │                              │
│  ai_bridge.c                │  COM2   │  ai_listener.py              │
│  ├─ ai_bridge_init()        │◄──────►│  ├─ TCP socket (port 4444)   │
│  ├─ ai_send_event(EVT)      │──EVT──►│  ├─ AIEngine.run_loop()      │
│  ├─ ai_poll_cmd()           │◄──CMD──│  ├─ dispatch_event()         │
│  └─ ai_engine_query()       │──REQ──►│  └─ send_response()          │
│                             │◄──RESP─│                              │
│  ai_engine.c                │         │  modules/                    │
│  └─ high-level query API    │         │  └─ system_monitor.py        │
└─────────────────────────────┘         └──────────────────────────────┘
         │
    QEMU -serial tcp::4444
    (COM2 → TCP Bridge)
```

QEMU ทำหน้าที่เป็น Bridge แปลง COM2 Serial Output เป็น TCP Connection ที่ Python สามารถ connect ได้โดยตรง

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | บทบาท |
|------|------|--------|
| `include/ai_bridge.h` | C Header | Frame structs, constants, API declarations |
| `kernel/drivers/ai_bridge.c` | C | UART init, frame encode/decode, polling |
| `kernel/drivers/ai_engine.c` | C | High-level query API, timeout handling |
| `ai/ai_listener.py` | Python 3.12 | Main loop, TCP connect, frame dispatch |
| `ai/corelib.py` | Python 3.12 | AIEngine class, module registration |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. COM2 UART Setup

ใช้ **COM2 (0x2F8)** แทน COM1 เพราะ COM1 ถูก Serial Console ใช้อยู่แล้ว

```c
#define AI_COM2_PORT        0x2F8
#define AI_BAUD_RATE        115200
#define AI_SCRATCH_REG      (AI_COM2_PORT + 7)  // Offset 7 = Scratch Register

// ตรวจ COM2 มีอยู่จริงด้วย Scratch Register Test
bool ai_bridge_init(void) {
    outb(AI_SCRATCH_REG, 0xAB);          // เขียน magic byte
    uint8_t val = inb(AI_SCRATCH_REG);   // อ่านกลับ
    if (val != 0xAB) return false;       // ไม่มี COM2

    uart_init(AI_COM2_PORT, AI_BAUD_RATE);
    return true;
}
```

### 2. Protocol Frames (Text-Based, Newline-Terminated)

ออกแบบให้เป็น Human-readable เพื่อ Debug ได้ง่ายผ่าน `nc localhost 4444`

```
┌────────────────────────────────────────────────────────────┐
│                  Frame Format                              │
├────────────────────────────────────────────────────────────┤
│  TYPE|field1|field2|...\n                                  │
│                                                            │
│  REQ  │ "REQ|<seq>|<query_text>\n"                        │
│       │   Python → Kernel (ส่ง Query)                     │
│                                                            │
│  RESP │ "RESP|<seq>|<response_text>\n"                    │
│       │   Kernel → Python (ส่งคำตอบ)                      │
│                                                            │
│  EVT  │ "EVT|<type>|<data>\n"                             │
│       │   Kernel → Python (ส่ง Kernel Events)             │
│                                                            │
│  CMD  │ "CMD|<seq>|<command>|<params>\n"                  │
│       │   Python → Kernel (ส่งคำสั่งจาก AI)               │
└────────────────────────────────────────────────────────────┘
```

**ตัวอย่าง Frames จริง:**

```
EVT|BOOT|vernisOS-x86_64-v0.8
EVT|PROC|pid=3,name=shell,type=system
EVT|DENY|pid=5,syscall=28,cap=MODULE_LOAD
EVT|STAT|proc_count|4
EVT|STAT|memory_used_pct|62
REQ|0042|what is the memory usage?
RESP|0042|Memory usage is at 62% with 4 active processes
CMD|0001|get_process_list|all
```

### 3. Event Types ที่ Kernel ส่ง

| Event Type | ข้อมูลที่ส่ง | ตัวอย่าง Data |
|-----------|------------|--------------|
| `BOOT` | Version string | `vernisOS-x86_64-v0.8` |
| `PROC` | Process info | `pid=3,name=shell,type=system` |
| `MOD` | Module load/unload | `action=load,name=vfs` |
| `EXCP` | Exception/Fault | `type=pagefault,addr=0xDEAD` |
| `DENY` | Syscall/Capability denial | `pid=5,syscall=28` |
| `FAIL` | Kernel error | `component=ipc,code=-ENOMEM` |
| `STAT` | System statistics | `proc_count\|4` |
| `SYSCALL` | Syscall trace (debug) | `pid=2,num=20,args=...` |

### 4. STAT Event Payloads

```
EVT|STAT|proc_count|<N>
EVT|STAT|memory_used_pct|<N>
EVT|STAT|ipc_queue_len|<N>
```

ส่งทุก 100 ticks (1 วินาที ที่ PIT 100Hz) เพื่อให้ Python AI Engine มีข้อมูลสถานะระบบแบบ Real-time

---

## โครงสร้างข้อมูล / API หลัก

### ai_bridge.h — Constants และ Frame Sizes

```c
#define AI_MAX_QUERY_LEN     256
#define AI_MAX_RESPONSE_LEN  512
#define AI_MAX_EVENT_DATA    128
#define AI_FRAME_SEQ_DIGITS  4
#define AI_TIMEOUT_TICKS     500   // 5 วินาที ที่ 100Hz

#define AI_POLL_INTERVAL     10    // poll ทุก 10 ticks
#define AI_STAT_INTERVAL     100   // ส่ง STAT ทุก 100 ticks
```

### C API Functions

```c
// เริ่มต้น COM2 (ตรวจ Scratch Register ก่อน)
bool ai_bridge_init(void);

// ส่ง Event ไปยัง Python
void ai_send_event(const char* type, const char* data);

// อ่าน CMD Frame จาก COM2 (เรียกจาก IRQ0)
void ai_poll_cmd(void);

// ส่ง Query และรอรับ Response (blocking พร้อม timeout)
bool ai_engine_query(const char* query_buf,
                     char* resp_buf,
                     uint32_t max_resp);
```

### ai_engine_query() — Sequence Diagram

```
Kernel                                Python AI Engine
  │                                        │
  │── "REQ|0042|what is memory?\n" ──────►│
  │                                        │  process query
  │                                        │  call LLM / corelib
  │◄─── "RESP|0042|Memory is 62%\n" ──────│
  │                                        │
  │  (ถ้าไม่ได้รับใน AI_TIMEOUT_TICKS)    │
  │  return false (timeout)               │
```

---

## ขั้นตอนการทำงาน

### IRQ0 Integration (PIT 100Hz)

```c
// ใน irq0_handler (kernel/arch/x86/irq.c)
static uint32_t tick_counter = 0;

void irq0_handler(void) {
    tick_counter++;

    // AI Command Polling — ทุก 10ms
    if (tick_counter % AI_POLL_INTERVAL == 0) {
        ai_poll_cmd();
    }

    // STAT Event — ทุก 1 วินาที
    if (tick_counter % AI_STAT_INTERVAL == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf), "proc_count|%u",
                 scheduler_get_process_count());
        ai_send_event("STAT", buf);
    }

    scheduler_tick();
    pic_send_eoi(0);
}
```

### ai_poll_cmd() — Frame Parser

```c
void ai_poll_cmd(void) {
    static char frame_buf[256];
    static int  frame_pos = 0;

    // อ่านทุก byte ที่มีอยู่ใน UART Buffer
    while (uart_data_available(AI_COM2_PORT)) {
        char c = uart_read_byte(AI_COM2_PORT);

        if (c == '\n') {
            frame_buf[frame_pos] = '\0';
            ai_dispatch_cmd(frame_buf);   // Parse "CMD|seq|cmd|params"
            frame_pos = 0;
        } else if (frame_pos < 255) {
            frame_buf[frame_pos++] = c;
        }
    }
}
```

### QEMU Build Target

```makefile
# make run64-ai — รัน QEMU พร้อม COM2 ต่อกับ TCP 4444
run64-ai:
	$(QEMU_X64) \
	    -kernel $(KERNEL_X64_BIN) \
	    -serial stdio \
	    -serial tcp::4444,server,nowait \
	    $(QEMU_FLAGS)
```

```bash
# Terminal 1: รัน QEMU
make run64-ai

# Terminal 2: รัน Python AI Engine
python3 ai/ai_listener.py
```

---

## ผลลัพธ์

| รายการ | ผลลัพธ์ |
|--------|---------|
| COM2 UART Init (Scratch Register Detection) | ✅ ตรวจพบ COM2 และ fallback ถ้าไม่มี |
| Protocol Frames (REQ/RESP/EVT/CMD) | ✅ Text-based, Human-readable |
| ai_send_event() | ✅ ส่ง EVT ทุกประเภทได้ถูกต้อง |
| ai_poll_cmd() ใน IRQ0 | ✅ ไม่ Block Kernel Loop |
| ai_engine_query() + Timeout | ✅ รอได้สูงสุด 5 วินาที |
| STAT Events ทุก 1 วินาที | ✅ proc_count, memory_pct, ipc_queue_len |
| make run64-ai | ✅ COM2 ต่อ TCP 4444 ผ่าน QEMU |

---

## สิ่งที่ต่อใน Phase ถัดไป

**Phase 9 — Python AI Engine: Corelib + Listener** จะพัฒนาฝั่ง Python เพื่อ:
- รับ EVT frames ทุกประเภทที่ Kernel ส่งมาและ dispatch ไปยัง AI Modules
- `system_monitor.py` จะ subscribe STAT events เพื่อ track สถานะระบบ
- `AIEngine.send_request()` จะส่ง REQ frame และรอ RESP ตาม sequence number
- Plugin system ให้เพิ่ม AI Module ได้โดยไม่แก้ core code
