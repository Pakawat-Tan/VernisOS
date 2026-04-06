# VernisOS — สถาปัตยกรรมระบบ

## ภาพรวม

VernisOS คือ **Microkernel OS** แบบ bare-metal ที่มี AI engine ฝังอยู่ใน kernel
รองรับทั้ง **x86 (32-bit)** และ **x86_64 (64-bit)** จาก disk image เดียว โดยตรวจจับ CPU ณ เวลา boot

```
┌─────────────────────────────────────────────┐
│                CLI / Terminal               │  Phase 7
├─────────────────────────────────────────────┤
│  Policy Enforcement  │  Auth (SHA-256)      │  Phase 12-13
├──────────────────────┤  UserDB + VernisFS   │
│  Sandbox (bitmask)   ├──────────────────────┤
│  Syscall filtering   │  Audit Log + Klog    │  Phase 14-16
├──────────────────────┴──────────────────────┤
│           AI Engine (Rust no_std)           │  Phase 10-11
│  EventStore │ AnomalyDetector │ AutoTuner   │
├─────────────────────────────────────────────┤
│  Scheduler (BTreeMap)  │  IPC (Queue+Chan)  │  Phase 3-4
├─────────────────────────────────────────────┤
│  Module Loader  │  Memory Manager  │ GDT/IDT│  Phase 3,5
├─────────────────────────────────────────────┤
│  Bootloader (3-stage)  │  BIOS INT 13h      │  Phase 2
└─────────────────────────────────────────────┘
```

---

## การบูทระบบ

### Bootloader 3 ขั้นตอน

```
เปิดเครื่อง → BIOS → MBR (Stage 1)
                       │
                       ├─ CPUID → ตรวจพบ x86_64?
                       │    ใช่ → โหลด Stage 3 → Long Mode → โหลด kernel x64
                       │    ไม่  → โหลด Stage 2 → Protected Mode → โหลด kernel x86
                       │
                       └─ เข้า kernel_main() ที่ 0x100000
```

| Stage | ไฟล์ | ขนาด | ที่อยู่โหลด | หน้าที่ |
|-------|------|------|------------|---------|
| 1 | `stage1.asm` | 512B | 0x7C00 | MBR: ตรวจ CPUID, แยกเส้นทาง Stage 2 หรือ 3 |
| 2 | `stage2.asm` | 2KB | 0x8000 | x86: โหลด kernel จาก disk, เข้า Protected Mode |
| 3 | `stage3.asm` | 2KB | 0x9000 | x64: ตั้ง paging (PML4), เข้า Long Mode, โหลด kernel |

### Disk Layout

```
Sector 0            : Boot Stage 1 (MBR, 512B)
Sectors 1-5         : Boot Stage 2 (2.5KB)
Sectors 6-11        : Boot Stage 3 (3KB)
Sectors 12-1211     : Kernel x86 (~614KB)
Sectors 2048-3583   : Kernel x64 (~768KB)
Sector 4096         : Policy VPOL blob (~804B)
Sector 5120+        : VernisFS (superblock + file table + data)
```

ขนาด disk image รวม: **4MB** (sparse — ส่วนใหญ่ว่าง)

---

## แผนที่หน่วยความจำ

| ที่อยู่ | ขนาด | เนื้อหา |
|---------|------|---------|
| `0x0000` | 1KB | Real-mode IVT |
| `0x1000` | 4KB | PML4 page table (x64 เท่านั้น) |
| `0x2000` | 4KB | PDPT |
| `0x3000` | 4KB | Page Directory (4×2MB pages) |
| `0x7C00` | 512B | Stage 1 bootloader |
| `0x8000` | 2KB | Stage 2 bootloader |
| `0x9000` | 2KB | Stage 3 bootloader |
| `0xB8000` | 4KB | VGA text buffer (80×25) |
| `0x100000` | ~124KB | Kernel binary (entry point) |
| `0x200000` | 2MB | Kernel heap (Rust allocator) |
| `0x500000` | — | Kernel stack top |

**Paging (x64):** 4-level, 2MB huge pages, identity-mapped 0–8MB

---

## ลำดับการ Init Kernel

```
kernel_main():
  1.  ตั้ง stack pointer → 0x500000
  2.  เคลียร์ BSS section
  3.  serial_init() + terminal_initialize()
  4.  verniskernel_init_heap()        ← Rust heap allocator
  5.  gdt_init() + idt_init()         ← Exception handling
  6.  pic_init() + pit_init(100Hz)    ← Timer + interrupts
  7.  keyboard_init()
  8.  verniskernel_register_print()   ← Rust print callback
  9.  syscall_init() + scheduler_new()
  10. ipc_init()                      ← Message queues + channels
  11. module_init()                   ← Module loader
  12. sandbox_init()                  ← Syscall sandboxing
  13. ai_bridge_init()                ← AI IPC bridge
  14. ai_kernel_engine_init()         ← In-kernel Rust AI engine
  15. policy_load_from_disk()         ← VPOL policy blob
  16. vfs_init() + userdb_init()      ← VernisFS + user database
  17. auditlog_init() + klog_init()   ← Logging systems
  18. cli_shell_init()                ← CLI session (root)
  19. sti                             ← เปิด interrupts
  20. cli_shell_loop()                ← Interactive shell
```

---

## สถาปัตยกรรมภาษาโปรแกรม

| Layer | ภาษา | เหตุผล |
|-------|------|--------|
| Bootloader | Assembly (NASM) | ควบคุม hardware โดยตรง |
| Kernel core | C | Low-level kernel, drivers, CLI |
| Scheduler + AI | Rust (`no_std`) | ความปลอดภัย, ไม่มี heap fragmentation |
| AI tools | Python | Policy compiler, test harness |

### C ↔ Rust FFI

Rust ถูก compile เป็น `staticlib` (`libvernisos.a`) แล้ว link เข้ากับ C objects

ฟังก์ชัน FFI หลัก:
```c
// Heap + print registration
extern void verniskernel_init_heap(uint64_t start, uint64_t size);
extern void verniskernel_register_print(void (*cb)(const uint8_t*, uint32_t));

// Scheduler
extern void *scheduler_new(void);
extern uint32_t scheduler_create_process(void *sched, uint8_t priority, const char *cmd);
extern uint32_t scheduler_schedule(void *sched);

// AI Engine
extern void *ai_engine_new(void);
extern void ai_engine_feed_event(void *engine, ...);
extern uint8_t ai_engine_check_access(const void *engine, const uint8_t *cmd, size_t len);
```

---

## สถาปัตยกรรม AI Engine

AI engine วิ่งทั้งหมดใน kernel เป็น Rust `no_std` staticlib:

```
Events (C) → ai_engine_feed_event() → Rust AI Engine
                                         │
                                         ├─ EventStore (ring buffer, 256 entries)
                                         ├─ AnomalyDetector (rate/pattern/threshold)
                                         ├─ ProcessTracker (per-PID trust scoring)
                                         ├─ AutoTuner (load-based quantum adjustment)
                                         └─ ResponseHandler → callbacks → C kernel
                                              ├─ tune: scheduler quantum/priority
                                              └─ remediate: kill/throttle processes
```

### ประเภท Event

| Code | ชื่อ | แหล่งที่มา |
|------|------|----------|
| 1 | BOOT | Kernel เริ่มต้น |
| 2 | STAT | สถิติตามช่วงเวลา |
| 3 | EXCP | CPU exceptions |
| 4 | PROC | Process lifecycle |
| 5 | MOD | Module load/unload |
| 6 | DENY | การปฏิเสธสิทธิ์ |
| 7 | FAIL | ความล้มเหลวของระบบ |
| 8 | SYSCALL | Syscall events |

---

## โมเดล Privilege และความปลอดภัย

### ประเภท Process

| ประเภท | Ring | Syscall Mask | ตัวอย่าง |
|--------|------|-------------|---------|
| KERNEL | Ring 0 | ทั้งหมด (bits 0-32) | init, ai_engine |
| SYSTEM | Ring 1 | 0-2, 20-27 | Filesystem, network |
| USER | Ring 3 | 0-2, 20-22 | Applications |

### การตรวจสอบสิทธิ์ใน CLI (2 ชั้น)

1. **Static**: เปรียบเทียบ `min_privilege` ของ command กับ privilege ของ session ปัจจุบัน
2. **Dynamic**: ตรวจสอบ AI policy rules ผ่าน `policy_check_command()`

การปฏิเสธจะถูกบันทึกไปยังทั้ง audit log และ AI engine

---

## คำสั่ง CLI (18 คำสั่ง)

| คำสั่ง | สิทธิ์ | คำอธิบาย |
|--------|--------|---------|
| help | USER | แสดงความช่วยเหลือ |
| clear | USER | เคลียร์หน้าจอ |
| info | USER | ข้อมูลระบบ |
| exit | USER | ออกจาก shell |
| whoami | USER | ผู้ใช้ปัจจุบัน |
| ps | USER | รายการ process |
| echo | USER | พิมพ์ข้อความ |
| login | USER | เปลี่ยนผู้ใช้ |
| su | USER | รันในฐานะ root |
| logout | USER | กลับ default |
| ai | ADMIN | สอบถาม AI engine |
| users | ADMIN | รายการผู้ใช้ |
| auditlog | ADMIN | ดู audit log |
| log | ADMIN | ดู kernel log |
| shutdown | ROOT | ปิดเครื่อง |
| restart | ROOT | รีบูต |
| policy | ROOT | ดู/โหลด policy |
| test | ROOT | รัน self-tests |
