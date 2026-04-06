# VernisOS — สถาปัตยกรรม Kernel

> ไฟล์ที่เกี่ยวข้อง: `kernel/arch/x86/kernel_x86.c`, `kernel/arch/x86_64/kernel_x64.c`

---

## ภาพรวม

VernisOS สร้าง kernel 2 ไฟล์แยกกัน แต่แชร์โค้ดส่วนใหญ่ (driver, fs, security) ผ่านการ compile เดียวกัน:

| ไฟล์ | สถาปัตยกรรม | Entry point | Stack |
|------|------------|-------------|-------|
| `kernel_x86.c` | i686 (32-bit) | `kernel_main()` ที่ `0x100000` | ESP = `0x500000` |
| `kernel_x64.c` | x86-64 (64-bit) | `kernel_main()` ที่ `0x100000` | RSP = `0x500000` |

---

## Kernel Init Sequence

ลำดับการ initialize ระบบใน `kernel_main()` (ห้ามสลับ — มี dependency):

```
1.  ตั้ง Stack Pointer → 0x500000
2.  Zero BSS segment (_bss_start → _bss_end)
3.  Serial COM1 init (0x3F8, 115200 baud) — debug output
4.  VGA Text Mode init (0xB8000, 80×25)
5.  Rust Heap init → verniskernel_init_heap(heap_addr, 2MB)
6.  Register print callback → verniskernel_register_print()
7.  GDT init → CPU descriptor tables
8.  IDT init → Interrupt handler tables
9.  PIC init → 8259A remap (IRQ0→0x20, IRQ8→0x28)
10. PIT init → 100 Hz timer (IRQ0)
11. Keyboard init → PS/2 ring buffer
12. syscall_init() → Rust syscall dispatch table
13. ipc_init() → Message queue + Channel IPC
14. module_init() → Dynamic module registry
15. sandbox_init() → Capability-based process sandbox
16. ai_bridge_init() → COM2 UART check + AI protocol
17. ai_kernel_engine_init() → In-kernel Rust AI engine
18. policy_load_from_disk() → โหลด VPOL จาก Sector 4096
19. vfs_init() → Mount VernisFS จาก Sector 5120
20. userdb_init() → โหลด users จาก /etc/shadow
21. auditlog_init() → Security audit log
22. klog_init() → Kernel structured log
23. CLI init → สร้าง root session
24. Scheduler init → สร้าง PID 1 (init), PID 2 (ai_engine)
25. sti → เปิด interrupts
26. cli_shell_loop() → วน loop รอ user input (ค้างอยู่ที่นี่ตลอด)
```

---

## ความแตกต่างระหว่าง x86 และ x64

| Feature | x86 (32-bit) | x86_64 (64-bit) |
|---------|-------------|------------------|
| Register width | 32-bit (eax, esp, ...) | 64-bit (rax, rsp, ...) |
| GDT | 5 entries (flat 32-bit) | 5 entries + TSS descriptor |
| TSS | ไม่มี | มี (RSP0=kernel stack, IST1-3) |
| IDT entry size | 8 bytes | 16 bytes (64-bit handler ptr) |
| ISR stub | `pusha` + cdecl | ручной push r8-r15, rdi, rsi, ... |
| SYSCALL | INT 0x80 เท่านั้น | INT 0x80 + SYSCALL/SYSRET |
| SSE/SSE2 | ไม่ต้องตั้ง | ตั้ง CR0/CR4 ก่อน Rust ทำงาน |
| Stack frame | `InterruptFrame32` | `InterruptFrame` (64-bit) |

---

## Global Descriptor Table (GDT)

### x86 GDT (5 entries)

| Index | Selector | Type | DPL | คำอธิบาย |
|-------|---------|------|-----|----------|
| 0 | `0x00` | null | — | Null descriptor (required) |
| 1 | `0x08` | Code | 0 | Kernel code (DPL=0, execute/read) |
| 2 | `0x10` | Data | 0 | Kernel data (DPL=0, read/write) |
| 3 | `0x18` | Code | 3 | User code (DPL=3, สำหรับ Ring 3 ในอนาคต) |
| 4 | `0x20` | Data | 3 | User data (DPL=3) |

### x64 GDT (เพิ่ม TSS)

เหมือน x86 แต่มี entry เพิ่ม:

| Index | Selector | Type | คำอธิบาย |
|-------|---------|------|----------|
| 5–6 | `0x28` | TSS | 16-byte TSS descriptor (64-bit ต้องการ 2 GDT slots) |

**TSS (Task State Segment)** ใน x64:
- `rsp[0]` = Kernel Ring-0 stack (16 KB) — ใช้ตอน interrupt จาก Ring 3
- `ist[0]` = Double-Fault stack (8 KB) — IST1
- `ist[1]` = NMI stack (4 KB) — IST2

---

## Interrupt Descriptor Table (IDT)

256 entries แบ่งเป็น:

| Vector | ชนิด | คำอธิบาย |
|--------|------|----------|
| 0–31 | CPU Exceptions | #DE, #DB, #NMI, #BP, #OF, #BR, #UD, #NM, **#DF**(8), #TS, #NP, #SS, **#GP**(13), **#PF**(14), #MF, #AC, #MC, #XM, ... |
| 0x20 (32) | IRQ0 | PIT Timer 100 Hz |
| 0x21 (33) | IRQ1 | PS/2 Keyboard |
| 0x28–0x2F | IRQ8–15 | Secondary PIC |
| 0x80 (128) | Trap gate | Syscall (INT 0x80), DPL=3 |

### Interrupt Dispatch Flow

```
Hardware Interrupt / INT instruction
        ↓
IDT → isr_stub (interrupts.asm)
  - บันทึก registers (pusha / manual push)
  - เรียก interrupt_dispatch(frame)
        ↓
interrupt_dispatch() ใน kernel_x86.c / kernel_x64.c
  ┌─ vec == 0x20 (Timer):
  │    kernel_tick++
  │    EOI → 0x20
  │    ทุก 10 ticks: ai_poll_cmd()
  │    ทุก 100 ticks: ai_send_event("STAT", ...)
  │
  ├─ vec == 0x21 (Keyboard):
  │    keyboard_handle_scancode(inb(0x60))
  │
  ├─ vec == 0x80 (Syscall):
  │    c_syscall_handler(frame)
  │      ├─ num 20–27: ipc_syscall()
  │      ├─ num 28–32: module_syscall()
  │      ├─ num 40–42: ai_syscall()
  │      └─ อื่นๆ: syscall_handler() [Rust]
  │
  └─ vec 0–31 (Exception):
       พิมพ์ "EXCEPTION vec=N rip=0x..." แล้ว hlt
```

---

## Memory Layout ขณะ Kernel รัน

```
0x00000000 – 0x000FFFFF  Real Mode + ROM + BIOS (ไม่ใช้)
0x00100000               Kernel Binary (.text เริ่มที่นี่)
0x00100000 – 0x0047xxxx  Kernel code + data + BSS
0x00480000               Kernel heap (2 MB static array ใน BSS)
0x00500000               Kernel stack top (ESP/RSP ตั้งไว้ที่นี่)
0x00B8000                VGA text buffer (80×25)
```

### Heap

Kernel heap เป็น static array ขนาด 2 MB ใน BSS section:
```c
static uint8_t kernel_heap[2 * 1024 * 1024];
```
ส่งให้ Rust Buddy Allocator จัดการผ่าน `verniskernel_init_heap(addr, size)`

---

## Timer (PIT) Configuration

PIT (Programmable Interval Timer, Intel 8253/8254) ตั้งที่ **100 Hz**:

```c
// Divisor = 1193182 / 100 = 11931
outb(0x43, 0x36);          // Channel 0, mode 3, binary
outb(0x40, 11931 & 0xFF);  // Low byte
outb(0x40, 11931 >> 8);    // High byte
```

ทุก tick (10ms):
- increment `kernel_tick`
- ทุก 10 ticks (100ms): `ai_poll_cmd()` ตรวจ CMD จาก Python
- ทุก 100 ticks (1s): ส่ง STAT event ไปยัง Python

---

## C ↔ Rust FFI

C เรียก Rust ผ่าน `extern "C"` functions:

```c
// Heap
extern void verniskernel_init_heap(uintptr_t start, uintptr_t size);
extern void verniskernel_register_print(void (*cb)(const uint8_t*, uint32_t));

// Scheduler
extern void    *scheduler_new(void);
extern uint32_t scheduler_create_process(void *sched, uint8_t priority, const char *cmd);
extern uint32_t scheduler_schedule(void *sched);
extern uint32_t scheduler_get_process_count(const void *sched);
extern void     scheduler_set_quantum(void *sched, uint32_t ms);
extern void     scheduler_set_priority(void *sched, uint32_t pid, uint8_t pri);

// Syscall
extern void    syscall_init(void);
extern int64_t syscall_handler(uint32_t num, uint64_t a1, uint64_t a2, uint64_t a3);

// AI Engine
extern void     ai_kernel_engine_init(void *scheduler);
extern void     ai_kernel_engine_feed(const char *evt, const char *data, uint64_t now);
extern void     ai_kernel_engine_tick(uint64_t now);
extern uint32_t ai_kernel_engine_anomaly_count(void);
extern uint8_t  ai_kernel_engine_check_access(const char *cmd, size_t len);
```

---

## PS/2 Keyboard

Keyboard IRQ1 อ่าน scancode จาก port `0x60` แล้วแปลงเป็น ASCII:

```
IRQ1 → isr33 → interrupt_dispatch → keyboard_handle_scancode(byte)
  → แปลง scancode set 1 → ASCII
  → เก็บใน ring buffer (256 bytes)
  → cli_readline() ดึงออกผ่าน keyboard_read_char()
```

รองรับ: Shift, Caps Lock, Ctrl, Alt, Backspace, Delete, Home, End, Arrow keys (ผ่าน escape sequence)

---

## x64-specific: SYSCALL/SYSRET

x64 ตั้ง MSR สำหรับ `SYSCALL` instruction:

```c
// IA32_EFER: ตั้ง SCE (Syscall Enable) bit
wrmsr(0xC0000080, rdmsr(0xC0000080) | (1 << 0));

// STAR: kernel CS = 0x08, user CS = 0x18
wrmsr(0xC0000081, (0x0008ULL << 32) | (0x0018ULL << 48));

// LSTAR: handler address
wrmsr(0xC0000082, (uint64_t)&syscall_entry);

// SFMASK: clear IF + TF เมื่อเข้า syscall
wrmsr(0xC0000084, (1 << 9) | (1 << 8));
```

`syscall_entry` (syscall.asm) บันทึก register, เรียก `c_syscall_handler`, แล้ว `sysretq`
