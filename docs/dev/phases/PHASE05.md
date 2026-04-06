# Phase 5 — Module Loader

> สัปดาห์ 11–13 | ภาษา: C + Rust (no_std) | สถานะ: ⏳

---

## เป้าหมาย

สร้างระบบ Module Loader ที่ช่วยให้ VernisOS โหลด/ยกเลิก kernel module แบบ dynamic (ในขอบเขตที่ static linking รองรับ) — ใช้ Rust สำหรับ registry ที่ type-safe และ C เป็น wrapper สำหรับ syscall interface พร้อม capability check จาก sandbox

---

## ภาพรวม

Microkernel ต้องการกลไก extend functionality โดยไม่ต้อง rebuild kernel ใหม่ทุกครั้ง Phase 5 วางรากฐานของระบบนี้ด้วย **static module registry** (8 slots) ที่สามารถ register/unregister module ได้ในขณะ runtime

```
┌─────────────────────────────────────────────────────────┐
│                   Module Lifecycle                      │
│                                                         │
│  syscall 28 (SYS_MOD_LOAD)                              │
│       │                                                 │
│       ▼                                                 │
│  capability check: CAP_MODULE_LOAD?                     │
│       │ YES                                             │
│       ▼                                                 │
│  module_load(name, init_fn, fini_fn)   [C wrapper]      │
│       │                                                 │
│       ▼                                                 │
│  module_registry_load(name, init, fini)  [Rust]         │
│       │                                                 │
│       ├─ หา slot ว่างใน registry[8]                     │
│       ├─ เก็บ name + fn pointers                        │
│       ├─ set loaded = true                              │
│       └─ เรียก init_fn()                               │
│                                                         │
│  syscall 29 (SYS_MOD_UNLOAD)                            │
│       │                                                 │
│       ▼                                                 │
│  module_unload(name)                                    │
│       ├─ หา slot ด้วย name                              │
│       ├─ เรียก fini_fn()                               │
│       └─ clear slot, loaded = false                     │
└─────────────────────────────────────────────────────────┘
```

**สถานะปัจจุบัน:** Registry และ static module ทำงานได้ แต่ dynamic linking (ELF loading จาก disk) ยังอยู่ระหว่างพัฒนา

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | หน้าที่ |
|------|------|---------|
| `kernel/module/module.c` | C | Wrapper syscall, capability check |
| `kernel/include/module.h` | C | Struct + API declarations |
| `kernel/core/verniskernel/src/module_registry.rs` | Rust | Registry implementation (8 slots) |
| `kernel/include/sandbox.h` | C | Capability definitions |
| `kernel/core/verniskernel/src/lib.rs` | Rust | FFI export ของ registry functions |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. Module Struct และ Registry (Rust)

```rust
// kernel/core/verniskernel/src/module_registry.rs
#![allow(dead_code)]

const MAX_MODULES: usize = 8;
const MAX_NAME:    usize = 32;

#[repr(C)]
pub struct ModuleEntry {
    pub name:    [u8; MAX_NAME],
    pub init_fn: Option<extern "C" fn() -> i32>,
    pub fini_fn: Option<extern "C" fn()>,
    pub loaded:  bool,
}

impl ModuleEntry {
    const fn empty() -> Self {
        ModuleEntry {
            name:    [0u8; MAX_NAME],
            init_fn: None,
            fini_fn: None,
            loaded:  false,
        }
    }
}

static mut REGISTRY: [ModuleEntry; MAX_MODULES] = [
    ModuleEntry::empty(),
    ModuleEntry::empty(),
    ModuleEntry::empty(),
    ModuleEntry::empty(),
    ModuleEntry::empty(),
    ModuleEntry::empty(),
    ModuleEntry::empty(),
    ModuleEntry::empty(),
];
```

### 2. FFI Functions (Rust → C)

```rust
// kernel/core/verniskernel/src/module_registry.rs (continued)

/// โหลด module ใหม่: ลงทะเบียนและเรียก init_fn
/// Return: slot index (0–7) หรือ -1 ถ้า registry เต็มหรือ init fail
#[no_mangle]
pub extern "C" fn module_registry_load(
    name:    *const u8,
    init_fn: Option<extern "C" fn() -> i32>,
    fini_fn: Option<extern "C" fn()>,
) -> i32 {
    unsafe {
        // หา slot ว่าง
        let slot = REGISTRY.iter().position(|e| !e.loaded);
        let slot = match slot {
            Some(s) => s,
            None    => return -1,  // registry เต็ม
        };

        // copy name (null-terminated)
        let mut n = [0u8; MAX_NAME];
        let mut i = 0;
        while i < MAX_NAME - 1 {
            let c = *name.add(i);
            if c == 0 { break; }
            n[i] = c;
            i += 1;
        }

        REGISTRY[slot].name    = n;
        REGISTRY[slot].init_fn = init_fn;
        REGISTRY[slot].fini_fn = fini_fn;
        REGISTRY[slot].loaded  = true;

        // เรียก init_fn
        if let Some(f) = init_fn {
            let ret = f();
            if ret != 0 {
                // init ล้มเหลว — ยกเลิก
                REGISTRY[slot] = ModuleEntry::empty();
                return -2;
            }
        }
        slot as i32
    }
}

/// ยกเลิก module: เรียก fini_fn และ clear slot
#[no_mangle]
pub extern "C" fn module_registry_unload(name: *const u8) -> i32 {
    unsafe {
        let slot = find_slot_by_name(name);
        if slot < 0 { return -1; }
        let s = slot as usize;
        if let Some(f) = REGISTRY[s].fini_fn {
            f();
        }
        REGISTRY[s] = ModuleEntry::empty();
        0
    }
}

/// หา slot ด้วยชื่อ — return slot index หรือ -1
#[no_mangle]
pub extern "C" fn module_registry_find(name: *const u8) -> i32 {
    unsafe { find_slot_by_name(name) }
}

unsafe fn find_slot_by_name(name: *const u8) -> i32 {
    for (i, entry) in REGISTRY.iter().enumerate() {
        if !entry.loaded { continue; }
        // เปรียบเทียบ name
        let mut j = 0;
        loop {
            let a = entry.name[j];
            let b = *name.add(j);
            if a != b { break; }
            if a == 0 { return i as i32; }
            j += 1;
            if j >= MAX_NAME { break; }
        }
    }
    -1
}
```

### 3. C Wrapper (module.c)

```c
// kernel/module/module.c
#include "module.h"
#include "sandbox.h"   // capability definitions

// FFI declarations
extern int  module_registry_load(const char *name,
                                  int  (*init_fn)(void),
                                  void (*fini_fn)(void));
extern int  module_registry_unload(const char *name);
extern int  module_registry_find(const char *name);

// เรียกจาก kernel_main init sequence
void module_init(void) {
    // registry ถูก init ที่ compile time (static array ใน Rust)
    // ไม่ต้องทำอะไรเพิ่มใน C side
}

// Public API — C side เป็น thin wrapper
int module_load(const char *name,
                int  (*init_fn)(void),
                void (*fini_fn)(void))
{
    return module_registry_load(name, init_fn, fini_fn);
}

int module_unload(const char *name) {
    return module_registry_unload(name);
}

int module_find(const char *name) {
    return module_registry_find(name);
}

// Syscall handlers สำหรับ SYS_MOD_LOAD / UNLOAD / FIND / EXEC
// เรียกจาก syscall.rs dispatch table
int sys_mod_load(uint64_t name_ptr, uint64_t init_ptr,
                 uint64_t fini_ptr, uint64_t _unused)
{
    // ตรวจ capability ก่อน
    if (!process_has_capability(current_pid(), CAP_MODULE_LOAD))
        return -EPERM;
    return module_load((const char *)name_ptr,
                       (int (*)(void))init_ptr,
                       (void (*)(void))fini_ptr);
}

int sys_mod_unload(uint64_t name_ptr, uint64_t _a2,
                   uint64_t _a3,      uint64_t _a4)
{
    if (!process_has_capability(current_pid(), CAP_MODULE_LOAD))
        return -EPERM;
    return module_unload((const char *)name_ptr);
}

int sys_mod_find(uint64_t name_ptr, uint64_t _a2,
                 uint64_t _a3,      uint64_t _a4)
{
    return module_find((const char *)name_ptr);
}

int sys_mod_exec(uint64_t slot_id, uint64_t _a2,
                 uint64_t _a3,     uint64_t _a4)
{
    // exec: เรียก init_fn ของ slot ที่ระบุซ้ำ (สำหรับ reload)
    // TODO: implement
    (void)slot_id;
    return -ENOSYS;
}
```

### 4. Capability Check (sandbox.h)

```c
// kernel/include/sandbox.h (excerpt)
#define CAP_MODULE_LOAD   (1 << 0)  // โหลด/ยกเลิก module ได้
#define CAP_NET_BIND      (1 << 1)  // bind network port
#define CAP_FS_WRITE      (1 << 2)  // เขียน filesystem
#define CAP_DEVICE_IO     (1 << 3)  // direct I/O port access
// ...

bool process_has_capability(uint32_t pid, uint32_t cap);
```

### 5. ตัวอย่าง Module จริง

```c
// ตัวอย่าง: net_module ที่จะโหลดผ่าน module system
// kernel/drivers/net/net_module.c

static int net_init(void) {
    // init network stack
    serial_print("[net] initializing...\n");
    return 0;  // 0 = success
}

static void net_fini(void) {
    serial_print("[net] shutting down\n");
}

// ลงทะเบียนผ่าน syscall หรือ kernel init
void register_net_module(void) {
    module_load("net", net_init, net_fini);
}
```

---

## โครงสร้างข้อมูล / API หลัก

### Module Header

```c
// kernel/include/module.h
#ifndef MODULE_H
#define MODULE_H

#include <stdint.h>

#define MODULE_NAME_MAX  32
#define MODULE_MAX_COUNT  8

typedef int  (*module_init_fn_t)(void);
typedef void (*module_fini_fn_t)(void);

// Public API
void module_init(void);
int  module_load(const char *name,
                 module_init_fn_t init_fn,
                 module_fini_fn_t fini_fn);
int  module_unload(const char *name);
int  module_find(const char *name);

#endif
```

### Syscall Table (28–31)

| Syscall# | ชื่อ | พารามิเตอร์ | ต้องการ Capability | ผลลัพธ์ |
|----------|------|-----------|-----------------|---------|
| 28 | `SYS_MOD_LOAD` | name, init_fn, fini_fn | `CAP_MODULE_LOAD` | slot/-errno |
| 29 | `SYS_MOD_UNLOAD` | name | `CAP_MODULE_LOAD` | 0/-errno |
| 30 | `SYS_MOD_FIND` | name | ไม่จำกัด | slot/-errno |
| 31 | `SYS_MOD_EXEC` | slot_id | `CAP_MODULE_LOAD` | 0/-errno |

### Registry Layout

```
REGISTRY[0]: { name="fs",   init=fs_init,   fini=fs_fini,   loaded=true  }
REGISTRY[1]: { name="net",  init=net_init,  fini=net_fini,  loaded=false }
REGISTRY[2]: { name="ai",   init=ai_init,   fini=ai_fini,   loaded=false }
REGISTRY[3]: { name="",     init=NULL,      fini=NULL,      loaded=false }
...
REGISTRY[7]: { name="",     init=NULL,      fini=NULL,      loaded=false }
```

---

## ขั้นตอนการทำงาน

### โหลด Module ใหม่

```
User Process (has CAP_MODULE_LOAD)
  │
  │  syscall(28, "fs", &fs_init, &fs_fini)
  ▼
sys_mod_load()
  │
  ├─ 1. ตรวจ capability: process_has_capability(pid, CAP_MODULE_LOAD)
  │      ถ้าไม่มี → return -EPERM
  │
  ├─ 2. module_registry_load("fs", fs_init, fs_fini)  [Rust]
  │
  ├─ 3. หา slot ว่างใน REGISTRY[0..7]
  │      ถ้าไม่มี → return -1 (registry เต็ม)
  │
  ├─ 4. copy name → REGISTRY[slot].name
  │      set init_fn, fini_fn
  │      set loaded = true
  │
  ├─ 5. เรียก init_fn()
  │      ถ้า return != 0 → undo, clear slot, return -2
  │
  └─ 6. return slot index (0–7)
```

### ยกเลิก Module

```
syscall(29, "fs", 0, 0)
  │
  ▼
sys_mod_unload("fs")
  │
  ├─ 1. capability check
  ├─ 2. module_registry_unload("fs")  [Rust]
  ├─ 3. find_slot_by_name("fs") → slot
  ├─ 4. เรียก fini_fn()
  └─ 5. REGISTRY[slot] = empty  → loaded = false
```

---

## ผลลัพธ์ (สิ่งที่ทำได้แล้ว)

- Static module registry (8 slots) ใน Rust ทำงานได้
- `module_load` / `module_unload` / `module_find` ผ่าน C wrapper
- Syscall 28–31 ลงทะเบียนใน syscall table แล้ว
- Capability check ด้วย `CAP_MODULE_LOAD` จาก `sandbox.h`
- FS module และ Net module โหลดได้แบบ static (compile-time linked)
- ทดสอบบน QEMU: load "fs" → fs_init() รัน → find "fs" → unload → fini รัน

## สิ่งที่ยังไม่สมบูรณ์ (งาน TODO)

```
[TODO] Dynamic ELF Loading
  ─ อ่าน .ko file จาก VernisFS
  ─ parse ELF header (ET_REL)
  ─ relocate symbols (R_X86_64_*)
  ─ bind kernel symbols (kallsyms-like table)
  ─ jump to module init entry

[TODO] SYS_MOD_EXEC (syscall 31)
  ─ exec specific slot หรือ named function ใน module

[TODO] Module dependency resolution
  ─ mod A depends on mod B → load B ก่อน

[TODO] แก้ registry size
  ─ 8 slots อาจไม่พอสำหรับ Phase 9+ (AI modules)
  ─ ขยายเป็น dynamic Vec หลัง heap allocator stable
```

---

## ข้อจำกัดและหนี้ทางเทคนิค

| ข้อจำกัด | เหตุผล | แผนแก้ไข |
|----------|---------|----------|
| Registry 8 slots เท่านั้น | Static array ง่ายกว่า dynamic | Dynamic Vec ใน Phase 9 |
| ไม่มี dynamic linking | ELF relocation ซับซ้อน | Phase 7+ หลัง FS พร้อม |
| ไม่มี module dependency | ยังไม่ต้องการใน phase นี้ | Phase 9 AI modules |
| `sys_mod_exec` ยัง ENOSYS | ยังไม่ clear semantics | Phase 6 sandbox review |

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 6 implement **User Sandbox Environment** — แยก privilege ring, ตั้ง per-process capability table (sandbox.h), และ enforce capability check ที่ทุก syscall ที่ sensitive รวมถึง `SYS_MOD_LOAD` ที่ Phase 5 เพิ่งเพิ่มมา
