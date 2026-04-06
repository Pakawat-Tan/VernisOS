# Phase 2 — Bootloader (BIOS)

> สัปดาห์ 3–5 | ภาษา: Assembly (NASM) | สถานะ: ✅

---

## เป้าหมาย

สร้าง 3-stage bootloader สำหรับ BIOS boot ที่รองรับทั้ง x86 (32-bit Protected Mode) และ x86_64 (64-bit Long Mode) โดยตรวจสอบ CPU capability ตั้งแต่ Stage1 และเลือก kernel image ที่เหมาะสมอัตโนมัติ

---

## ภาพรวม

BIOS โหลด Stage1 จาก sector 0 ไปที่ `0x7C00` และกระโดดไปรัน Stage1 ตรวจสอบ CPU ด้วย `CPUID` แล้วเขียน flag ไว้ที่ `0x7FF0` เพื่อให้ stage ถัดไปอ่านได้ จากนั้นโหลด Stage2 → Stage3 ตามลำดับ ซึ่ง Stage3 จะจัดการ paging และนำ kernel ขึ้น Long Mode

```
BIOS
  │
  ▼ โหลด 512 bytes → 0x7C00
┌──────────────────────┐
│  Stage1 (MBR)        │  sector 0  (512 B)
│  CPUID check         │
│  → cpu_mode @ 0x7FF0 │
│  INT 0x13 load S2    │
└──────────┬───────────┘
           │ โหลด sectors 1–5 → 0x8000
           ▼
┌──────────────────────┐
│  Stage2              │  sector 1–5 (~2 KB)
│  อ่าน cpu_mode       │
│  เปิด A20 line       │
│  โหลด Stage3 หรือ   │
│  x86 kernel          │
└──────────┬───────────┘
           │ [64-bit path] โหลด sector 6–11 → 0x9000
           ▼
┌──────────────────────┐
│  Stage3              │  sector 6–11 (~2 KB)
│  โหลด x64 kernel     │
│  ตั้ง PML4/PDPT/PD   │
│  เปิด Long Mode      │
│  copy 0x10000→0x100000│
│  jmp 0x100000        │
└──────────────────────┘
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | หน้าที่ |
|------|------|---------|
| `boot/x86/stage1.asm` | NASM x86-16 | MBR, CPUID, โหลด Stage2 |
| `boot/x86/stage2.asm` | NASM x86-16 | A20, เลือก path 32/64 bit |
| `boot/x86/stage3.asm` | NASM x86-16/32 | Paging, Long Mode, copy kernel |

---

## สิ่งที่พัฒนา (รายละเอียด)

### Stage1 — MBR (512 bytes @ 0x7C00)

Stage1 ต้องพอดีใน 512 bytes (รวม boot signature `0xAA55` ที่ท้าย)

```nasm
; boot/x86/stage1.asm (simplified)
[BITS 16]
[ORG 0x7C00]

start:
    ; ตั้ง segment registers
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; ตรวจสอบ Long Mode ด้วย CPUID
    mov eax, 0x80000001
    cpuid
    test edx, (1 << 29)       ; LM bit
    jz  .is_32bit

.is_64bit:
    mov word [0x7FF0], 1      ; cpu_mode = 1 (64-bit)
    jmp .load_stage2

.is_32bit:
    mov word [0x7FF0], 2      ; cpu_mode = 2 (32-bit)

.load_stage2:
    ; โหลด Stage2 จาก LBA 1–5 ไปที่ 0x8000
    mov ah, 0x42              ; INT 0x13 Extended Read
    mov dl, 0x80              ; drive 0
    mov si, .dap
    int 0x13
    jc  .error
    jmp 0x0000:0x8000

.dap:                         ; Disk Address Packet
    db 0x10                   ; size
    db 0x00
    dw 5                      ; sectors to read
    dw 0x8000                 ; offset
    dw 0x0000                 ; segment
    dq 1                      ; start LBA

.error:
    hlt

times 510 - ($ - $$) db 0
dw 0xAA55
```

**flag ที่ address `0x7FF0`:**
- `1` = CPU รองรับ Long Mode → ใช้ kernel x64
- `2` = CPU 32-bit เท่านั้น → ใช้ kernel x86

---

### Stage2 — A20 + Path Selection (@ 0x8000)

```nasm
; boot/x86/stage2.asm (simplified)
[BITS 16]
[ORG 0x8000]

start2:
    ; อ่าน cpu_mode ที่ Stage1 เขียนไว้
    mov ax, [0x7FF0]
    cmp ax, 1
    je  .path_64bit

.path_32bit:
    ; เปิด A20
    in  al, 0x92
    or  al, 2
    out 0x92, al
    ; โหลด x86 kernel จาก LBA 12 (1200 sectors) → 0x10000
    ; แล้วเข้า Protected Mode โดยตรง
    ; ... (ตั้ง GDT minimal, set PE bit ใน CR0, far jmp)
    jmp enter_pm_32

.path_64bit:
    ; เปิด A20
    in  al, 0x92
    or  al, 2
    out 0x92, al
    ; โหลด Stage3 จาก LBA 6–11 → 0x9000
    ; ... DAP สำหรับ Stage3
    jmp 0x0000:0x9000
```

**การเปิด A20:** ใช้ Fast A20 port `0x92` (bit 1) วิธีนี้เร็วที่สุดแต่ไม่รองรับฮาร์ดแวร์เก่ามากๆ

---

### Stage3 — Long Mode Setup (@ 0x9000)

Stage3 เป็นขั้นตอนที่ซับซ้อนที่สุด: โหลด kernel → ตั้ง page table → เปิด Long Mode → copy kernel → jump

```nasm
; boot/x86/stage3.asm (simplified)
[BITS 16]
[ORG 0x9000]

start3:
    ; โหลด x64 kernel จาก LBA 2048
    ; 12 batches × 128 sectors = 1536 sectors → 0x10000
    ; (INT 0x13 มี limit 127 sectors ต่อครั้ง ต้อง loop)
    mov cx, 12                ; 12 batches
    mov ebx, 2048             ; start LBA
    mov edi, 0x10000          ; destination
.load_loop:
    ; ... Extended Read DAP loop
    loop .load_loop

    ; ตั้ง PML4 @ 0x1000
    ; ตั้ง PDPT @ 0x2000
    ; ตั้ง PD  @ 0x3000  (2MB huge pages, identity map 0–8MB)
    mov edi, 0x1000
    mov cr3, edi

    ; เปิด PAE (CR4.PAE)
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; ตั้ง EFER.LME (MSR 0xC0000080)
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; เปิด Paging + Protected Mode (CR0)
    mov eax, cr0
    or  eax, (1 << 31) | 1
    mov cr0, eax

    ; Far jump เข้า 64-bit code segment
    jmp 0x08:long_mode_entry

[BITS 64]
long_mode_entry:
    ; copy kernel จาก 0x10000 → 0x100000 ด้วย rep movsq
    mov rsi, 0x10000
    mov rdi, 0x100000
    mov rcx, (1536 * 512) / 8    ; จำนวน qwords
    rep movsq

    ; กระโดดไปยัง kernel entry
    mov rax, 0x100000
    jmp rax
```

**Page Table Layout:**

```
PML4 @ 0x1000:   entry[0] → PDPT @ 0x2000
PDPT @ 0x2000:   entry[0] → PD   @ 0x3000
PD   @ 0x3000:   entry[0] → 0x000000 (2MB, PS=1)   maps 0–2MB
                 entry[1] → 0x200000 (2MB, PS=1)   maps 2–4MB
                 entry[2] → 0x400000 (2MB, PS=1)   maps 4–6MB
                 entry[3] → 0x600000 (2MB, PS=1)   maps 6–8MB
```

---

## โครงสร้างข้อมูล / API หลัก

### Disk Layout

| Region | Sector เริ่ม | จำนวน Sectors | Load Address | ขนาด |
|--------|------------|--------------|-------------|------|
| Stage1 (MBR) | 0 | 1 | 0x7C00 | 512 B |
| Stage2 | 1 | 5 | 0x8000 | 2.5 KB |
| Stage3 | 6 | 6 | 0x9000 | 3 KB |
| Kernel x86 | 12 | 1200 | 0x10000 | 600 KB |
| (ว่าง) | 1212 | 836 | — | — |
| Kernel x64 | 2048 | 1536 | 0x10000→0x100000 | 768 KB |

### Memory Map (หลัง boot)

```
0x0000_0000 – 0x0000_03FF  IVT (BIOS, 1 KB)
0x0000_0400 – 0x0000_04FF  BDA (BIOS Data Area)
0x0000_1000 – 0x0000_1FFF  PML4 (4 KB)
0x0000_2000 – 0x0000_2FFF  PDPT (4 KB)
0x0000_3000 – 0x0000_3FFF  PD   (4 KB)
0x0000_7C00 – 0x0000_7DFF  Stage1 (512 B)
0x0000_7FF0              ← cpu_mode flag
0x0000_8000 – 0x0000_8FFF  Stage2
0x0000_9000 – 0x0000_9FFF  Stage3
0x0001_0000 – 0x0008_FFFF  Kernel load buffer (temp)
0x000A_0000 – 0x000F_FFFF  Video / ROM (BIOS)
0x0010_0000              ← Kernel entry point (1 MB mark)
0x0050_0000              ← Kernel stack top
```

---

## ขั้นตอนการทำงาน

1. BIOS POST → อ่าน sector 0 → วางที่ `0x7C00` → กระโดดไป
2. Stage1 รัน: ตรวจ CPUID Long Mode bit → เขียน `cpu_mode` ที่ `0x7FF0`
3. Stage1 ใช้ INT 0x13 AH=0x42 โหลด Stage2 (5 sectors) → `0x8000` → `jmp 0x8000`
4. Stage2 รัน: อ่าน `cpu_mode`
   - ถ้า `cpu_mode=2` (32-bit): โหลด x86 kernel → เข้า Protected Mode → `jmp 0x10000`
   - ถ้า `cpu_mode=1` (64-bit): เปิด A20 → โหลด Stage3 → `jmp 0x9000`
5. Stage3 รัน (path 64-bit):
   - โหลด x64 kernel (1536 sectors) จาก LBA 2048 → `0x10000` (12 batches loop)
   - ล้าง page table region `0x1000–0x3FFF`
   - สร้าง PML4/PDPT/PD สำหรับ identity map 0–8MB (2MB huge pages)
   - เปิด PAE → ตั้ง EFER.LME → เปิด Paging+PE → far jmp 64-bit segment
6. ใน Long Mode: `rep movsq` copy kernel จาก `0x10000` → `0x100000`
7. `jmp 0x100000` → kernel entry (`kernel_main` ใน C)

---

## ผลลัพธ์

- Boot สำเร็จทั้งบน QEMU (x86 และ x86_64)
- CPU detection ทำงานถูกต้อง เลือก kernel image อัตโนมัติ
- Long Mode เปิดพร้อม identity-mapped paging สำหรับ 8MB แรก
- Kernel ถูก copy ไปที่ `0x100000` และ entry point รันได้

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 3 เริ่มต้นที่ `kernel_main` ใน `kernel_x64.c` — ต้องตั้ง stack, zero BSS, init serial/VGA, จากนั้น call Rust `verniskernel_init_heap` เพื่อ bootstrap allocator ก่อนที่จะ init GDT, IDT, scheduler, และ IPC ตามลำดับ 26 ขั้นตอน
