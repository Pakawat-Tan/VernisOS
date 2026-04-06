# Phase 6 — User Sandbox Environment
> สัปดาห์ 14–16 | ภาษา: C + Rust (no_std) | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

สร้างระบบ **Sandbox สำหรับ User Process** ที่ทำให้ VernisOS สามารถรันโปรแกรมที่ไม่น่าเชื่อถือได้อย่างปลอดภัย โดยไม่กระทบเสถียรภาพของ Kernel โดยใช้ระบบ Capability Bitmask, Syscall Whitelist, และการแยก Memory Layout ต่อ Process

---

## ภาพรวม

Phase 6 วางรากฐานความปลอดภัยของ VernisOS ด้วย 3 กลไกหลัก:

```
┌─────────────────────────────────────────────────────┐
│              VernisOS Security Model                │
├─────────────────────────────────────────────────────┤
│  [Capability Bitmask] — สิทธิ์ระดับ Operation       │
│  [Syscall Whitelist]  — กรอง Syscall ตาม ProcType   │
│  [Memory Isolation]   — แยก Address Space ต่อ PID   │
└─────────────────────────────────────────────────────┘
```

ทั้งสามกลไกทำงานร่วมกัน: เมื่อ User Process เรียก Syscall ระบบจะตรวจสอบ Capability ก่อน แล้วจึงตรวจ Whitelist และตรวจว่า Pointer ชี้ไปยัง Memory ของ Process นั้นจริงหรือไม่

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ประเภท | บทบาท |
|------|--------|--------|
| `include/sandbox.h` | Header | โครงสร้าง SecurityContext, Capability constants, API |
| `kernel/security/sandbox.c` | C Source | Logic การตรวจสอบ Syscall, Pointer validation |
| `kernel/core/verniskernel/src/scheduler.rs` | Rust | เพิ่ม field ความปลอดภัยใน PCB |
| `kernel/core/verniskernel/src/lib.rs` | Rust | Export FFI functions สำหรับ C |
| `Makefile` | Build | เพิ่ม sandbox.c ใน compilation rules |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. CPU Privilege Rings (x86/x86_64)

```
Ring 0 — PRIV_RING_KERNEL   ████████████  Kernel เต็มสิทธิ์
Ring 1 — PRIV_RING_DRIVER   ░░░░░░░░░░░░  สำรองไว้ (Driver)
Ring 2 — PRIV_RING_SYSTEM   ░░░░░░░░░░░░  System Services
Ring 3 — PRIV_RING_USER     ░░░░          User Applications (จำกัดมาก)
```

### 2. ProcessType enum

```c
typedef enum {
    PROC_TYPE_KERNEL = 0,   // PID 0, Core ระบบ
    PROC_TYPE_SYSTEM = 1,   // Services เช่น IPC, Driver managers
    PROC_TYPE_USER   = 2    // User applications (sandboxed)
} ProcessType;
```

### 3. Capability Bitmask (64-bit)

แต่ละ Process มี `capabilities` เป็น bitmask 64-bit ควบคุมสิทธิ์ระดับ Operation:

| Capability | Bitmask | ความหมาย |
|-----------|---------|-----------|
| `CAP_SERIAL_WRITE` | `0x01` | เขียนข้อมูลไปยัง Serial Port |
| `CAP_SERIAL_READ` | `0x02` | อ่านข้อมูลจาก Serial Port |
| `CAP_VGA_WRITE` | `0x04` | เขียนลง VGA Memory โดยตรง |
| `CAP_ALLOC_MEMORY` | `0x10` | จัดสรร Heap Memory |
| `CAP_MAP_MEMORY` | `0x20` | Map Physical Memory |
| `CAP_IPC_SEND` | `0x1000` | ส่ง IPC Message |
| `CAP_IPC_RECV` | `0x2000` | รับ IPC Message |
| `CAP_MODULE_LOAD` | `0x10000` | โหลด Kernel Module |
| `CAP_MODULE_EXECUTE` | `0x40000` | Execute Module Function |
| `CAP_SYS_DEBUG` | `0x100000` | ดู Debug State ของระบบ |

**Default Capability Sets:**

```
KERNEL  → CAP_KERNEL_ALL  = 0xFFFFFFFFFFFFFFFF  (ทุก Cap)
SYSTEM  → CAP_SYSTEM_DEFAULT = IPC + MEM + DEBUG
USER    → CAP_USER_DEFAULT   = SERIAL_WRITE + IPC_SEND/RECV + ALLOC_MEMORY
```

### 4. Static Security Context Pool

```c
#define MAX_SECURITY_CONTEXTS 16

static SecurityContext security_pool[MAX_SECURITY_CONTEXTS];
```

จัดสรรแบบ Static Pool เพื่อหลีกเลี่ยง Dynamic Allocation ใน Kernel Path ที่อาจ deadlock ได้

---

## โครงสร้างข้อมูล / API หลัก

### SecurityContext struct

```c
typedef struct {
    uint32_t      pid;              // Process ID
    ProcessType   proc_type;        // Kernel / System / User
    PrivilegeRing runtime_ring;     // Ring 0–3
    uint64_t      capabilities;     // Bitmask สิทธิ์

    // Memory Isolation
    uint32_t      user_mem_base;    // 0x1000000 + (pid * 4MB)
    uint32_t      user_mem_size;    // 4MB (0x400000)

    // Resource Limits
    uint32_t      max_file_size;    // ขนาดไฟล์สูงสุด (bytes)
    uint32_t      max_open_files;   // จำนวน FD สูงสุด
    uint32_t      max_memory;       // Heap สูงสุด (bytes)

    // Audit Counters
    uint64_t      syscalls_made;    // จำนวน Syscall ที่เรียก
    uint64_t      capability_denials; // จำนวนครั้งที่ถูกปฏิเสธ
} SecurityContext;
```

### API Functions

```c
// ค้นหา/สร้าง Context
SecurityContext* sandbox_get_context(uint32_t pid);
SecurityContext* sandbox_create_context(uint32_t pid, ProcessType type);

// ตรวจสอบ Syscall
bool sandbox_check_syscall(const SecurityContext* ctx, uint32_t syscall_num);

// ตรวจสอบ Pointer
bool sandbox_validate_user_pointer(const SecurityContext* ctx,
                                   const void* ptr, uint32_t size);
```

### Rust PCB Fields (scheduler.rs)

```rust
pub struct ProcessControlBlock {
    // ... existing fields ...
    pub process_type:    ProcessType,
    pub privilege_ring:  PrivilegeRing,
    pub capabilities:    u64,
    pub user_mem_base:   usize,
    pub user_mem_size:   usize,
    pub cap_denials:     u64,
}
```

---

## ขั้นตอนการทำงาน

### Syscall Gate Flow

```
User Process เรียก Syscall
         │
         ▼
  sandbox_check_syscall(ctx, syscall_num)
         │
    ┌────┴────┐
    │ KERNEL? │──YES──▶  อนุญาตทันที (ทุก Syscall)
    └────┬────┘
         │ NO
    ┌────▼────────────────────────────────┐
    │  ตรวจ Whitelist ตาม ProcessType     │
    │  Kernel: 0–32 ทั้งหมด              │
    │  System: 0,1,2,20–27              │
    │  User  : 0,1,2,20–22             │
    └────┬────────────────────────────────┘
         │
    ┌────▼─────────┐
    │ IPC (20–22)? │──YES──▶ ต้องมี CAP_IPC_SEND/RECV
    └────┬─────────┘
    ┌────▼──────────────┐
    │ Module (28–31)?   │──YES──▶ ต้องมี CAP_MODULE_LOAD
    └────┬──────────────┘
         │
         ▼
    return true/false
```

### User Memory Layout

```
Physical Memory (per User PID):
┌─────────────────────────────────────┐
│  0x1000000 + (pid × 0x400000)       │  ← User Heap Base
│                                     │
│  [Code / Data Segment]              │
│  [Heap grows upward ↑]              │
│                                     │
│  [Stack grows downward ↓]           │
│  top = base + 4MB - 4KB            │  ← Stack Top
└─────────────────────────────────────┘
```

ตัวอย่าง: PID 1 → base = 0x1400000, PID 2 → base = 0x1800000

---

## ผลลัพธ์

| รายการ | ผลลัพธ์ |
|--------|---------|
| Capability System | ✅ ทำงานได้ทั้ง x86 และ x86_64 |
| Syscall Filtering | ✅ Whitelist บังคับใช้ก่อนทุก Syscall |
| Memory Isolation Layout | ✅ แยก Address Space ต่อ PID |
| FFI C ↔ Rust | ✅ scheduler_create_user_process(), scheduler_has_capability() |
| Static Pool (16 Contexts) | ✅ ไม่มี Dynamic Allocation ใน Kernel |
| Build Integration | ✅ sandbox.c คอมไพล์ใน x86 และ x86_64 |

---

## สิ่งที่ต่อใน Phase ถัดไป

**Phase 7 — CLI / Terminal System** จะใช้ SecurityContext จาก Phase 6 เพื่อ:
- ตรวจสอบสิทธิ์ผู้ใช้ก่อนรัน Built-in Command
- กำหนด `CliSession.privilege` ให้สอดคล้องกับ `ProcessType`
- Block คำสั่ง privileged (เช่น `shutdown`, `policy`) จาก User Session
- บันทึก Audit Log เมื่อมีการพยายามเรียก Command ที่ไม่มีสิทธิ์
