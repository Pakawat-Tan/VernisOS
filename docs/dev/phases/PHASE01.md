# Phase 1 — วางสถาปัตยกรรมและเอกสาร

> สัปดาห์ 1–2 | ภาษา: Markdown / Draw.io | สถานะ: ✅

---

## เป้าหมาย

วางพิมพ์เขียว (blueprint) ของระบบปฏิบัติการ VernisOS ทั้งหมดก่อนลงมือเขียนโค้ด ครอบคลุมสถาปัตยกรรม Microkernel, การแบ่ง Phase 17 ขั้น, ข้อกำหนดภาษา, และโครงสร้างไดเรกทอรี

---

## ภาพรวม

Phase นี้ไม่มีการเขียนโค้ดแม้แต่บรรทัดเดียว — เป็น **Design-Only Phase** ที่มุ่งเน้นการออกแบบเชิงสถาปัตยกรรมก่อนเริ่มพัฒนาจริง เหตุผลที่ต้องทำก่อนคือ:

- Microkernel มีขอบเขตที่เข้มงวดระหว่าง kernel space กับ user space
- ต้องกำหนดให้ชัดว่าส่วนใดเขียนใน Assembly, C, หรือ Rust
- การเปลี่ยนสถาปัตยกรรมกลางคันมีต้นทุนสูงมากใน bare-metal development

ผลผลิตของ Phase 1 คือชุดเอกสารที่ทีมทุกคน (หรือตัวเองในอนาคต) อ่านแล้วเข้าใจภาพรวมได้ทันที

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | หน้าที่ |
|------|------|---------|
| `README.md` | Markdown | แนะนำโปรเจกต์, วิธีบิลด์, วิธีรัน |
| `ARCHITECTURE.md` | Markdown | แผนภาพ Layer และการไหลของข้อมูล |
| `docs/dev/PHASES.md` | Markdown | ตาราง 17 Phase พร้อม timeline และสถานะ |
| `docs/dev/LANGUAGE_MAP.md` | Markdown | ข้อกำหนดภาษาต่อ component |
| `docs/design/*.drawio` | Draw.io XML | แผนภาพ memory layout, boot flow, IPC |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. สถาปัตยกรรม Microkernel

VernisOS ออกแบบตามหลัก Microkernel ที่ kernel มีขนาดเล็กที่สุดเท่าที่เป็นไปได้ โดย kernel ทำหน้าที่เพียง:

- **Memory Management** — จัดการ physical/virtual memory
- **Process Scheduling** — สลับ context ระหว่าง process
- **IPC** — ส่งข้อความระหว่าง process
- **Interrupt Handling** — รับและกระจาย hardware interrupt

ทุกอย่างที่เหลือ (filesystem, driver, networking) อยู่ใน **Module** ที่รันใน kernel space แต่แยกออกจาก core

```
┌─────────────────────────────────────────────────────────────┐
│                        USER SPACE                           │
│   [CLI Process]   [User Apps]   [System Services]           │
└──────────────────────────┬──────────────────────────────────┘
                           │ Syscall Interface
┌──────────────────────────▼──────────────────────────────────┐
│                      KERNEL SPACE                           │
│  ┌─────────────┐  ┌──────────────┐  ┌──────────────────┐    │
│  │  Scheduler  │  │   Memory Mgr │  │   IPC Engine     │    │
│  │  (Rust)     │  │   (Rust)     │  │   (C)            │    │
│  └─────────────┘  └──────────────┘  └──────────────────┘    │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Arch Layer  (C)                         │   │
│  │   GDT / IDT / PIC / PIT / Serial / VGA               │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │              Module Registry (Rust + C)              │   │
│  │   [FS Module]  [Net Module]  [AI Module]             │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────┬──────────────────────────────────┘
                           │
┌──────────────────────────▼──────────────────────────────────┐
│                     BOOTLOADER                              │
│   Stage1 (MBR/ASM) → Stage2 (ASM) → Stage3 (ASM)            │
└─────────────────────────────────────────────────────────────┘
```

### 2. การแบ่ง Phase 17 ขั้น

| Phase | ชื่อ | สัปดาห์ | ภาษาหลัก |
|-------|------|---------|----------|
| 01 | วางสถาปัตยกรรม | 1–2 | Markdown |
| 02 | Bootloader BIOS | 3–5 | ASM |
| 03 | Core Kernel | 6–8 | C + Rust |
| 04 | IPC | 9–10 | C |
| 05 | Module Loader | 11–13 | C + Rust |
| 06 | User Sandbox | 14–15 | C |
| 07 | Filesystem | 16–18 | C + Rust |
| 08 | Networking | 19–21 | C |
| 09 | AI Engine Bridge | 22–24 | Python + C |
| 10 | AI Behavior Monitor | 25–26 | Python |
| 11–17 | (Advanced phases) | 27+ | TBD |

### 3. ข้อกำหนดภาษาต่อ Layer

```
Assembly (NASM):
  - ทุกอย่างที่ต้องการ raw hardware access ก่อน C runtime พร้อม
  - boot/x86/stage1.asm, stage2.asm, stage3.asm
  - kernel/arch/x86/interrupts.asm

C (freestanding, no libc):
  - Arch initialization (GDT, IDT, PIC, PIT)
  - IPC layer
  - Module wrapper
  - Driver stubs

Rust (no_std + no alloc ยกเว้น LockedHeap):
  - Scheduler (BTreeMap-based)
  - Memory allocator (buddy)
  - Syscall dispatch table
  - Module registry
  - ทุกส่วนที่ต้องการ type safety สูง
```

### 4. Memory Layout (ภาพรวม)

```
Physical Address Space (x86_64)
─────────────────────────────────────────
0x0000_0000  ← IVT / BDA (BIOS)
0x0000_7C00  ← Stage1 (MBR loaded here)
0x0000_8000  ← Stage2
0x0000_9000  ← Stage3
0x0001_0000  ← Kernel load buffer (temp)
0x0005_0000  ← Kernel stack top
0x0010_0000  ← Kernel entry (1 MB mark)
0x0010_2000  ← kernel_heap[2MB] (BSS)
0x0030_2000  ← (free for modules)
─────────────────────────────────────────
```

---

## โครงสร้างข้อมูล / API หลัก

ใน Phase นี้ยังไม่มี API จริง แต่กำหนด **interface contract** ไว้ล่วงหน้า:

```c
// สัญญาระหว่าง C ↔ Rust (FFI)
// ทุก function ที่ Rust export ต้องมี signature แบบนี้
extern void verniskernel_init_heap(void *heap_start, size_t heap_size);
extern void register_print(void (*fn)(const char*));
extern void syscall_init(void);
extern void scheduler_new(void);
extern pid_t scheduler_create_process(const char *name, void (*entry)(void));
extern pid_t scheduler_schedule(void);
```

---

## ขั้นตอนการทำงาน

1. ร่างแผนภาพ Layer ทั้งหมดใน Draw.io
2. เขียน `ARCHITECTURE.md` พร้อม ASCII diagram
3. สร้างตาราง Phase พร้อม timeline และ dependency
4. กำหนด language constraint ชัดเจนต่อทุก component
5. วาง directory structure และ naming convention
6. เขียน `README.md` ครอบคลุมวิธีบิลด์และรัน
7. Review และอนุมัติ blueprint ก่อนเริ่ม Phase 2

---

## ผลลัพธ์

- Blueprint สมบูรณ์ครอบคลุม 17 Phase
- ทุก component มี owner ภาษาชัดเจน
- Memory layout กำหนดไว้ล่วงหน้า ไม่ต้องเดาในภายหลัง
- FFI contract ระหว่าง C และ Rust ถูก draft ไว้แล้ว
- Directory structure พร้อมให้เริ่ม Phase 2 ทันที

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 2 เริ่ม implement 3-stage bootloader จริงใน Assembly (NASM) ตาม memory layout ที่กำหนดไว้ใน Phase 1 — ตั้งแต่ MBR (512 bytes) ไปจนถึงการเข้า Long Mode และกระโดดไปยัง kernel entry point ที่ `0x100000`
