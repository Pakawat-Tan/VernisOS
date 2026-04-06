# Phase 15 — Optimization (Kernel, Module, Sandbox)

> สัปดาห์ 39–42 | ภาษา: Rust + C | สถานะ: — (อนาคต)

---

## เป้าหมาย

ลดขนาด binary และเพิ่มประสิทธิภาพการทำงานของ kernel, module system, และ sandbox layer โดยไม่เปลี่ยน behavior ที่ผ่าน test แล้วใน Phase 14 เป้าหมายสุดท้าย: x86 binary < 200 KB, x64 binary < 250 KB

---

## ภาพรวม

```
ก่อน Optimize                      หลัง Optimize
─────────────────────────────────────────────────
x86 kernel binary: ~280 KB    →    < 200 KB  (-29%)
x64 kernel binary: ~340 KB    →    < 250 KB  (-26%)
IRQ0 handler latency: ~8 µs   →    < 5 µs
IPC throughput: ~50k msg/s    →    > 80k msg/s
Sandbox syscall fast-path:     →    skip cap check
  added overhead ~1.2 µs             overhead < 0.3 µs
```

การ optimize แบ่งเป็น 4 ด้านหลัก:

| ด้าน | เทคนิค | ผลที่คาดหวัง |
|------|--------|-------------|
| **C Binary Size** | `-Os`, dead code elimination | ลด 15–20% |
| **Rust Binary Size** | `opt-level="z"`, LTO, strip | ลด 20–30% |
| **IPC Performance** | SIMD copy, inline hot paths | +60% throughput |
| **Sandbox** | Fast-path syscall, skip cap check | -75% overhead |

---

## ไฟล์ที่เกี่ยวข้อง

```
Makefile                          # เพิ่ม -ffunction-sections -fdata-sections
kernel/
├── ipc/
│   └── ipc.c                     # SIMD memcpy path สำหรับ bulk transfer
├── sandbox/
│   └── sandbox.c                 # Fast-path สำหรับ trusted kernel syscalls
├── interrupt/
│   └── irq.c                     # Inline hot-path IRQ dispatch
rust_module/
└── Cargo.toml                    # [profile.release] opt settings
scripts/
└── size_report.sh                # NEW: แสดงขนาด binary ทุกชิ้น
```

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. C Compiler Optimizations

#### Flags ที่เพิ่มใน Makefile

```makefile
# ──── C optimization flags ────
CFLAGS_SIZE  = -Os                      # optimize for size
CFLAGS_LINK  = -ffunction-sections \    # แยก section ต่อ function
               -fdata-sections          # แยก section ต่อ global data
LDFLAGS_GC   = -Wl,--gc-sections        # ตัด section ที่ไม่ถูก reference

CFLAGS      += $(CFLAGS_SIZE) $(CFLAGS_LINK)
LDFLAGS     += $(LDFLAGS_GC)
```

ผลของ `--gc-sections`: linker ตัดทุก function/data ที่ไม่มีใครเรียกออก

```
ก่อน --gc-sections:            หลัง --gc-sections:
.text: 198 KB                  .text: 147 KB  (-26%)
  init_legacy_vga()  ← ไม่ใช้   ← ถูกตัด
  debug_dump_all()   ← ไม่ใช้   ← ถูกตัด
  test_uart_legacy() ← ไม่ใช้   ← ถูกตัด
```

#### Profile-Guided Manual Optimization

Hot paths ที่ระบุผ่าน GDB + QEMU profiling:

```c
/* IRQ0 handler — ถูกเรียก 100 ครั้ง/วินาที → inline */
static inline void __attribute__((always_inline))
irq0_tick_handler(void) {
    kernel_tick++;
    scheduler_tick();   /* inline ด้วย */
}

/* IPC send fast path — ถูกเรียกบ่อยมาก */
static inline int __attribute__((always_inline))
ipc_send_fast(ipc_channel_t *ch, const void *data, size_t len) {
    if (__builtin_expect(ch->count >= IPC_QUEUE_MAX, 0))
        return -EAGAIN;
    /* fast path: no lock needed for single-producer */
    ipc_enqueue_unsafe(ch, data, len);
    return 0;
}
```

---

### 2. Rust Optimizations

#### Cargo.toml — Release Profile

```toml
# rust_module/Cargo.toml

[profile.release]
opt-level    = "z"      # optimize for size (z = smallest, s = small+fast)
lto          = true     # Link Time Optimization: inlining ข้าม crate
codegen-units = 1       # single codegen unit → better inlining, slower compile
panic        = "abort"  # ไม่ใช้ unwind machinery → ลด binary ลงมาก
strip        = true     # ตัด debug symbols ออกจาก release binary
overflow-checks = false # ปิด runtime overflow check (เร็วกว่าเล็กน้อย)

[profile.dev]
opt-level = 0
debug     = true
```

#### ผลกระทบของแต่ละ setting

```
Rust module binary size breakdown:

  baseline (opt-level=3):           87 KB
  + opt-level="z":                  61 KB  (-30%)
  + lto=true:                       54 KB  (-38%)
  + codegen-units=1:                52 KB  (-40%)
  + panic="abort":                  44 KB  (-49%)
  + strip=true:                     31 KB  (-64%)
  ─────────────────────────────────────────
  TOTAL savings:                    56 KB  (-64%)
```

---

### 3. Module Optimization

#### ลบ unused modules + lazy init

```c
/* kernel/module/module_manager.c */

/* เดิม: init ทุก module ตอน boot */
void module_init_all(void) {
    init_vga_module();        /* ← ไม่ใช้ใน headless mode */
    init_ps2_module();        /* ← ไม่ใช้ถ้าไม่มี keyboard */
    init_legacy_timer();      /* ← ซ้ำซ้อนกับ HPET */
    init_debug_console();     /* ← ใช้เฉพาะ debug build */
}

/* หลัง optimize: lazy init + conditional compile */
void module_init_required(void) {
    init_serial();            /* ← ต้องใช้เสมอ */
    init_memory_manager();    /* ← ต้องใช้เสมอ */
    init_scheduler();         /* ← ต้องใช้เสมอ */
#ifdef CONFIG_VGA
    init_vga_module();
#endif
#ifdef CONFIG_PS2
    init_ps2_module();
#endif
}
```

---

### 4. Sandbox Optimization — Fast-Path Syscall

```
Sandbox syscall path เดิม:

  process call syscall N
         │
         ▼
  sandbox_dispatch()
         │
         ▼
  capability_check()   ← overhead ~1.2 µs (hash lookup + permission check)
         │
         ▼
  syscall_handler_N()

Fast-path (Phase 15): ข้ามการตรวจสอบสำหรับ trusted kernel syscalls

  process call syscall N
         │
         ▼
  sandbox_dispatch()
         │
         ├─ syscall N in TRUSTED_SET? ──YES──► syscall_handler_N()  [0.1 µs]
         │
         └─ NO ──► capability_check() ──► syscall_handler_N()      [1.3 µs]
```

```c
/* kernel/sandbox/sandbox.c */

/* Syscalls ที่ไม่ต้องตรวจ capability (kernel-internal) */
#define TRUSTED_SYSCALL_MASK  \
    ((1 << SYS_YIELD) |       \
     (1 << SYS_EXIT)  |       \
     (1 << SYS_GETPID))

int sandbox_dispatch(int syscall_num, ...) {
    /* Fast-path: ข้าม capability check */
    if (__builtin_expect(
            (TRUSTED_SYSCALL_MASK >> syscall_num) & 1, 1)) {
        return syscall_table[syscall_num](...);
    }
    /* Slow-path: full capability check */
    return sandbox_dispatch_checked(syscall_num, ...);
}
```

---

### 5. IPC Optimization — SIMD Copy

สำหรับ bulk transfer ที่ขนาด >= 64 bytes ใช้ SSE2 `movdqu` แทน `memcpy` ทั่วไป

```c
/* kernel/ipc/ipc.c */

#ifdef ARCH_X64
/* ตรวจว่า CPU รองรับ SSE2 */
static inline void ipc_memcpy_fast(void *dst, const void *src, size_t n) {
    if (n >= 64 && cpu_has_sse2()) {
        /* SSE2 copy: 16 bytes ต่อ instruction */
        __asm__ volatile(
            "movdqu (%1), %%xmm0\n"
            "movdqu %%xmm0, (%0)\n"
            /* ... unrolled 4x ... */
            : : "r"(dst), "r"(src) : "xmm0", "memory"
        );
    } else {
        __builtin_memcpy(dst, src, n);
    }
}
#else
#define ipc_memcpy_fast(d,s,n) __builtin_memcpy(d,s,n)
#endif
```

---

## โครงสร้างข้อมูล / API หลัก

```makefile
# make size-report: แสดงขนาด binary ทุกชิ้น

size-report:
	@echo "=== VernisOS Binary Size Report ==="
	@size build/kernel_x86.elf   | tail -1 | awk '{printf "x86  kernel: %d KB\n", $$4/1024}'
	@size build/kernel_x64.elf   | tail -1 | awk '{printf "x64  kernel: %d KB\n", $$4/1024}'
	@size build/module_rust.a    | tail -1 | awk '{printf "Rust module: %d KB\n", $$4/1024}'
	@echo "=== Targets: x86 < 200 KB, x64 < 250 KB ==="
```

```bash
# ตัวอย่าง output ของ make size-report:
=== VernisOS Binary Size Report ===
x86  kernel: 183 KB   [< 200 KB ✓]
x64  kernel: 241 KB   [< 250 KB ✓]
Rust module:  31 KB
=== Targets: x86 < 200 KB, x64 < 250 KB ===
```

---

## ขั้นตอนการทำงาน

### Profiling Workflow

```
1. Build debug binary (no optimization)
         │
         ▼
2. Run QEMU + GDB
   qemu-system-x86_64 -s -S ...
   gdb -ex "target remote :1234"
         │
         ▼
3. เปิด GDB dashboard, วัด latency:
   (gdb) break irq_handler
   (gdb) print $rip
   (gdb) continue  # วัดเวลา IRQ0 round-trip
         │
         ▼
4. ระบุ hot functions จาก GDB call count
         │
         ▼
5. Apply optimizations (inline, SIMD, etc.)
         │
         ▼
6. make size-report → ตรวจ binary size
         │
         ▼
7. make test-all → ยืนยัน correctness ยังเหมือนเดิม
```

### Checklist ก่อน merge

```
[ ] make size-report: x86 < 200 KB, x64 < 250 KB
[ ] make test-all: PASS (ทุก selftest + integration test)
[ ] IRQ0 latency < 5 µs (วัดด้วย QEMU + GDB)
[ ] IPC throughput > 80k msg/s (วัดด้วย test_ipc_bench)
[ ] Sandbox fast-path overhead < 0.3 µs
[ ] ไม่มี regression ใน auditlog, klog, permission
```

---

## ผลลัพธ์

| Metric | เป้าหมาย | วิธีวัด |
|--------|----------|---------|
| x86 binary size | < 200 KB | `make size-report` |
| x64 binary size | < 250 KB | `make size-report` |
| IRQ0 latency | < 5 µs | GDB + QEMU timing |
| IPC throughput | > 80k msg/s | `test_ipc_bench` |
| All tests pass | 100% | `make test-all` |

---

## สิ่งที่ต่อใน Phase ถัดไป

- **Phase 16**: Integration Test & Logging — ใช้ klog ที่ optimize แล้วเพื่อ log ผล optimization
- บันทึก benchmark result ลงใน `docs/benchmarks/` สำหรับเปรียบเทียบ release ต่อๆ ไป
- พิจารณา `-flto` สำหรับ C codebase (LTO ระหว่าง C objects)
- ทดสอบ `-march=i486` vs `-march=i686` สำหรับ x86 binary size
- Phase 17 (Developer Preview): binary sizes ต้องผ่าน target ก่อน release
