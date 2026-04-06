# Phase 3 — Core Kernel + Arch Layer

> สัปดาห์ 6–8 | ภาษา: C + Rust (no_std) | สถานะ: ✅

---

## เป้าหมาย

สร้าง kernel core ที่พร้อมใช้งานได้จริง: init sequence 26 ขั้นตอน, hardware abstraction layer (GDT/IDT/PIC/PIT), heap allocator ใน Rust, scheduler เบื้องต้น, และ syscall dispatch — ทั้งหมดนี้รองรับทั้ง x86 (32-bit) และ x86_64 (64-bit)

---

## ภาพรวม

เมื่อ bootloader กระโดดมาที่ `0x100000` control ส่งต่อมาให้ `kernel_main` ใน `kernel_x64.c` (หรือ `kernel_x86.c` สำหรับ 32-bit) ซึ่งต้องทำทุกอย่างตั้งแต่ศูนย์เพราะ BIOS ไม่ได้เตรียม C runtime ให้

```
0x100000: kernel_main()
    │
    ├── [1]  ตั้ง stack pointer @ 0x500000
    ├── [2]  zero BSS segment
    ├── [3]  init serial port (COM1, 115200 baud)
    ├── [4]  init VGA text mode (80×25)
    ├── [5]  verniskernel_init_heap()      ← Rust FFI
    ├── [6]  gdt_init()
    ├── [7]  idt_init()
    ├── [8]  pic_init()
    ├── [9]  pit_init(100)                 ← 100 Hz timer
    ├── [10] keyboard_init()
    ├── [11] register_print()             ← Rust FFI
    ├── [12] syscall_init()               ← Rust FFI
    ├── [13] scheduler_new()              ← Rust FFI
    ├── [14] ipc_init()
    ├── [15] security_init()
    ├── [16] fs_init()
    ├── [17–25] module/driver init
    └── [26] cli_run() / idle loop
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | หน้าที่ |
|------|------|---------|
| `kernel/arch/x86_64/kernel_x64.c` | C | Entry point x64, init sequence |
| `kernel/arch/x86/kernel_x86.c` | C | Entry point x86, init sequence |
| `kernel/arch/x86_64/gdt.c` | C | Global Descriptor Table |
| `kernel/arch/x86_64/idt.c` | C | Interrupt Descriptor Table |
| `kernel/arch/x86/pic.c` | C | PIC remapping (0x20/0x28) |
| `kernel/arch/x86/pit.c` | C | Programmable Interval Timer |
| `kernel/arch/x86/interrupts.asm` | NASM | ISR stubs (256 entries) |
| `kernel/core/verniskernel/src/lib.rs` | Rust | Heap init, FFI exports |
| `kernel/core/verniskernel/src/scheduler.rs` | Rust | Process scheduler |
| `kernel/core/verniskernel/src/memory.rs` | Rust | Buddy allocator |
| `kernel/core/verniskernel/src/syscall.rs` | Rust | Syscall dispatch table |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. GDT (Global Descriptor Table)

GDT กำหนด memory segment ที่ CPU ใช้ — ต้องตั้งก่อน IDT

```c
// kernel/arch/x86_64/gdt.c
typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) GdtEntry;

static GdtEntry gdt[5] = {
    // [0] Null descriptor
    {0, 0, 0, 0, 0, 0},
    // [1] Code32: base=0, limit=0xFFFFF, DPL=0, R/X
    {0xFFFF, 0, 0, 0x9A, 0xCF, 0},
    // [2] Data32: base=0, limit=0xFFFFF, DPL=0, R/W
    {0xFFFF, 0, 0, 0x92, 0xCF, 0},
    // [3] Code64: L=1 (64-bit), DPL=0
    {0x0000, 0, 0, 0x9A, 0xA0, 0},
    // [4] Data64: DPL=0, R/W
    {0x0000, 0, 0, 0x92, 0xA0, 0},
};

void gdt_init(void) {
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) gdtr;
    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base  = (uint64_t)gdt;
    __asm__ volatile("lgdt %0" :: "m"(gdtr));
    // reload segments
    __asm__ volatile(
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        "pushq $0x08\n"
        "lea 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n" ::: "rax"
    );
}
```

### 2. IDT (Interrupt Descriptor Table) + ISR Stubs

```c
// kernel/arch/x86_64/idt.c
typedef struct {
    uint16_t offset_low;
    uint16_t selector;     // code segment = 0x08
    uint8_t  ist;          // IST = 0
    uint8_t  type_attr;    // 0x8E = Present, DPL=0, Gate64
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) IdtEntry;

static IdtEntry idt[256];

// ISR stubs ใน interrupts.asm export ชื่อ isr0 .. isr255
extern void *isr_table[256];

void idt_init(void) {
    for (int i = 0; i < 256; i++) {
        uint64_t handler = (uint64_t)isr_table[i];
        idt[i].offset_low  = handler & 0xFFFF;
        idt[i].selector    = 0x08;
        idt[i].ist         = 0;
        idt[i].type_attr   = 0x8E;
        idt[i].offset_mid  = (handler >> 16) & 0xFFFF;
        idt[i].offset_high = (handler >> 32) & 0xFFFFFFFF;
        idt[i].zero        = 0;
    }
    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) idtr;
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint64_t)idt;
    __asm__ volatile("lidt %0" :: "m"(idtr));
}
```

**PIC Remapping** — เลื่อน IRQ0-7 → INT 0x20-0x27, IRQ8-15 → INT 0x28-0x2F:

```c
void pic_init(void) {
    // ICW1: cascade mode, ICW4 needed
    outb(0x20, 0x11);  outb(0xA0, 0x11);
    // ICW2: vector offset
    outb(0x21, 0x20);  outb(0xA1, 0x28);
    // ICW3: cascade config
    outb(0x21, 0x04);  outb(0xA1, 0x02);
    // ICW4: 8086 mode
    outb(0x21, 0x01);  outb(0xA1, 0x01);
    // mask all except IRQ0 (timer) + IRQ1 (keyboard)
    outb(0x21, 0xFC);  outb(0xA1, 0xFF);
}
```

### 3. Rust Kernel Core (no_std)

```rust
// kernel/core/verniskernel/src/lib.rs
#![no_std]
#![feature(alloc_error_handler)]

extern crate alloc;

use linked_list_allocator::LockedHeap;

#[global_allocator]
static ALLOCATOR: LockedHeap = LockedHeap::empty();

// FFI: C เรียก Rust เพื่อ init heap
#[no_mangle]
pub extern "C" fn verniskernel_init_heap(start: *mut u8, size: usize) {
    unsafe {
        ALLOCATOR.lock().init(start, size);
    }
}

// FFI: ลงทะเบียน print function จาก C เพื่อให้ Rust ใช้ได้
static mut PRINT_FN: Option<extern "C" fn(*const u8)> = None;

#[no_mangle]
pub extern "C" fn register_print(f: extern "C" fn(*const u8)) {
    unsafe { PRINT_FN = Some(f); }
}

#[alloc_error_handler]
fn alloc_error(layout: core::alloc::Layout) -> ! {
    panic!("alloc error: size={}", layout.size());
}
```

**Heap Region ใน BSS:**

```c
// kernel/arch/x86_64/kernel_x64.c
#define KERNEL_HEAP_SIZE (2 * 1024 * 1024)  // 2 MB

// วางใน BSS — linker จะ zero ให้ก่อน kernel_main
static uint8_t kernel_heap[KERNEL_HEAP_SIZE] __attribute__((aligned(4096)));

// เรียกใน init sequence ขั้นที่ 5
verniskernel_init_heap(kernel_heap, KERNEL_HEAP_SIZE);
```

### 4. Scheduler (Rust, BTreeMap-based)

```rust
// kernel/core/verniskernel/src/scheduler.rs
use alloc::collections::BTreeMap;
use alloc::string::String;

#[repr(C)]
pub struct Process {
    pub pid:   u32,
    pub name:  [u8; 32],
    pub state: ProcessState,
    pub entry: extern "C" fn(),
}

#[repr(C)]
pub enum ProcessState { Ready, Running, Blocked, Dead }

static mut SCHEDULER: Option<BTreeMap<u32, Process>> = None;
static mut NEXT_PID: u32 = 1;

#[no_mangle]
pub extern "C" fn scheduler_new() {
    unsafe { SCHEDULER = Some(BTreeMap::new()); }
}

#[no_mangle]
pub extern "C" fn scheduler_create_process(
    name: *const u8,
    entry: extern "C" fn()
) -> u32 {
    unsafe {
        let pid = NEXT_PID;
        NEXT_PID += 1;
        let mut proc_name = [0u8; 32];
        // copy name bytes...
        let p = Process { pid, name: proc_name, state: ProcessState::Ready, entry };
        SCHEDULER.as_mut().unwrap().insert(pid, p);
        pid
    }
}

#[no_mangle]
pub extern "C" fn scheduler_schedule() -> u32 {
    // Round-robin: หา process ที่ state=Ready ตัวแรก
    unsafe {
        if let Some(ref mut map) = SCHEDULER {
            for (pid, proc) in map.iter_mut() {
                if matches!(proc.state, ProcessState::Ready) {
                    proc.state = ProcessState::Running;
                    return *pid;
                }
            }
        }
        0
    }
}
```

### 5. Syscall Dispatch (Rust)

```rust
// kernel/core/verniskernel/src/syscall.rs
type SyscallFn = extern "C" fn(u64, u64, u64, u64) -> i64;

static mut SYSCALL_TABLE: [Option<SyscallFn>; 256] = [None; 256];

#[no_mangle]
pub extern "C" fn syscall_init() {
    unsafe {
        SYSCALL_TABLE[0]  = Some(sys_yield);
        SYSCALL_TABLE[1]  = Some(sys_exit);
        SYSCALL_TABLE[2]  = Some(sys_write);
        SYSCALL_TABLE[3]  = Some(sys_read);
        // ... syscalls 4–19 (memory, process)
        // syscalls 20–27 จะลงทะเบียนใน ipc_init (Phase 4)
        // syscalls 28–31 จะลงทะเบียนใน module_init (Phase 5)
    }
}

#[no_mangle]
pub extern "C" fn syscall_handler(
    num: u64, a1: u64, a2: u64, a3: u64
) -> i64 {
    unsafe {
        if let Some(f) = SYSCALL_TABLE[num as usize] {
            f(a1, a2, a3, 0)
        } else {
            -38  // ENOSYS
        }
    }
}
```

---

## โครงสร้างข้อมูล / API หลัก

### FFI Exports จาก Rust → C

```c
// ประกาศใน kernel/include/verniskernel.h
extern void verniskernel_init_heap(void *start, size_t size);
extern void register_print(void (*fn)(const char*));
extern void syscall_init(void);
extern int  syscall_handler(uint64_t num, uint64_t a1,
                            uint64_t a2, uint64_t a3);
extern void scheduler_new(void);
extern uint32_t scheduler_create_process(const char *name,
                                         void (*entry)(void));
extern uint32_t scheduler_schedule(void);
```

### Compiler Flags

```makefile
# x86_64 kernel
CFLAGS_X64 = -ffreestanding \
             -fno-pie \
             -fno-stack-protector \
             -mno-red-zone \
             -mcmodel=kernel \
             -mgeneral-regs-only \
             -O2 -std=c11

# Rust (no_std staticlib)
RUSTFLAGS   = -C panic=abort \
              -C opt-level=2
RUST_TARGET = x86_64-unknown-none
```

---

## ขั้นตอนการทำงาน

```
1.  kernel_main() เริ่มต้น
2.  ตั้ง RSP = 0x500000  (stack)
3.  zero BSS  (bss_start .. bss_end จาก linker script)
4.  serial_init(115200)  → COM1
5.  vga_init()           → 80×25 text mode, clear screen
6.  verniskernel_init_heap(kernel_heap, 2MB)   [Rust]
7.  gdt_init()           → lgdt, reload segments
8.  idt_init()           → lidt, 256 ISR stubs
9.  pic_init()           → remap IRQ 0x20/0x28
10. pit_init(100)        → IRQ0 = 100 Hz tick
11. keyboard_init()      → IRQ1 handler
12. register_print(kprintf)   [Rust]
13. syscall_init()            [Rust] → fill syscall table
14. scheduler_new()           [Rust] → create BTreeMap
15. ipc_init()           → init IPC queues/channels
16. security_init()      → init capability table
17. fs_init()            → init VFS / ramfs
18. module_init()        → init module registry
19–25. (optional drivers, net, AI bridge)
26. cli_run()  หรือ  idle_loop()
```

---

## ผลลัพธ์

- Kernel บูต สำเร็จบน QEMU ทั้ง 32-bit และ 64-bit
- Serial output ทำงาน (ใช้สำหรับ debug)
- VGA text mode แสดงข้อความได้
- Heap allocator พร้อมใช้งาน (Rust LockedHeap, 2MB)
- GDT/IDT/PIC/PIT ตั้งค่าถูกต้อง keyboard interrupt ทำงาน
- Scheduler สร้าง process ได้ (round-robin เบื้องต้น)
- Syscall dispatch table พร้อม (syscall 0–19)

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 4 implement IPC layer ใน `kernel/ipc/ipc.c` — เพิ่ม syscall 20–27 สำหรับ message queue และ channel communication ระหว่าง process โดยใช้ spinlock เพื่อ thread safety
