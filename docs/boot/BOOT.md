# VernisOS — Bootloader (โปรแกรมบูท)

> ไฟล์ที่เกี่ยวข้อง: `boot/x86/stage1.asm`, `stage2.asm`, `stage3.asm`

---

## ภาพรวม

Bootloader ของ VernisOS แบ่งออกเป็น **3 ขั้นตอน (3-stage)** เพื่อแก้ปัญหาขนาดจำกัดของ MBR
(512 bytes) และทำการเปลี่ยน CPU mode อย่างเป็นขั้นตอน

```
Power On → BIOS POST → โหลด MBR (Stage 1, 512B)
                           ↓
                  Stage 2 (Real Mode, 2 KB)
                  - ตรวจสอบ CPU mode (32/64-bit)
                  - โหลด Stage 3
                           ↓
                  Stage 3 (Protected → Long Mode, 2 KB)
                  - ตั้ง GDT / Paging
                  - เข้า 64-bit Long Mode
                  - โหลด kernel binary
                           ↓
                  kernel_main() ใน C
```

---

## Stage 1 — MBR Bootloader

**ไฟล์:** `boot/x86/stage1.asm`
**ขนาด:** 512 bytes (1 sector)
**ตำแหน่งบน disk:** Sector 0

### หน้าที่

Stage 1 เป็น MBR (Master Boot Record) มาตรฐาน BIOS จะโหลดมาไว้ที่ `0x7C00` โดยอัตโนมัติ

ขั้นตอนที่ทำ:
1. ตั้ง segment registers (`ds`, `es`, `ss`) ให้เป็น 0
2. ตั้ง stack pointer (`sp`) ไว้ต่ำกว่า `0x7C00`
3. ใช้ BIOS interrupt **INT 0x13** อ่าน Stage 2 จาก disk (LBA 1–5) มาไว้ที่ `0x8000`
4. ตรวจสอบว่า CPU รองรับ **Long Mode (64-bit)** หรือไม่ผ่าน CPUID:
   - ถ้ารองรับ → เขียน `cpu_mode = 1` (64-bit path)
   - ถ้าไม่รองรับ → เขียน `cpu_mode = 2` (32-bit path)
5. `jmp 0x8000` ไปยัง Stage 2

### ข้อจำกัด
ทำงานใน **Real Mode** (16-bit) ยังเข้าถึง memory ได้แค่ 1 MB
ต้องใช้ BIOS interrupt สำหรับ disk I/O

---

## Stage 2 — Extended Loader

**ไฟล์:** `boot/x86/stage2.asm`
**ขนาด:** ~2,048 bytes (padded)
**ตำแหน่งบน disk:** Sector 1–5
**โหลดไปที่:** `0x8000`

### หน้าที่

Stage 2 ยังทำงานใน Real Mode แต่มีพื้นที่มากขึ้น

ขั้นตอนที่ทำ:
1. อ่าน `cpu_mode` ที่ Stage 1 เขียนไว้
2. เปิด **A20 Line** (ผ่าน BIOS INT 0x15 / keyboard controller / port 0x92)
   - A20 ที่ปิดอยู่ทำให้ address bit 20 ถูกบังคับเป็น 0 → เข้าถึงหน่วยความจำเกิน 1 MB ไม่ได้
3. โหลด Stage 3 จาก disk (Sector 6–11) มาไว้ที่ `0x9000`
4. ถ้า `cpu_mode == 1` (64-bit): กระโดดไป Stage 3 เพื่อเข้า Long Mode
5. ถ้า `cpu_mode == 2` (32-bit): โหลด x86 kernel binary จาก Sector 12 ไปที่ `0x100000`
   แล้วกระโดดไปตรงๆ

---

## Stage 3 — Protected/Long Mode Setup

**ไฟล์:** `boot/x86/stage3.asm`
**ขนาด:** ~2,048 bytes (padded)
**ตำแหน่งบน disk:** Sector 6–11
**โหลดไปที่:** `0x9000`

### หน้าที่

Stage 3 ทำการเปลี่ยน CPU mode และโหลด kernel สุดท้าย

### ขั้นตอนโดยละเอียด

#### 1. เข้า Protected Mode (32-bit)
```nasm
; โหลด GDT ชั่วคราว (Flat 32-bit)
lgdt [gdt32_ptr]

; ตั้ง CR0.PE = 1
mov eax, cr0
or  eax, 1
mov cr0, eax

; Far jump เพื่อ flush CPU pipeline
jmp 0x08:protected_mode_entry
```

#### 2. ตั้ง Paging สำหรับ Long Mode
สร้าง Page Table ที่ `0x1000–0x4000` (ใช้ 2MB huge pages):

| Address | โครงสร้าง |
|---------|-----------|
| `0x1000` | PML4 (Page Map Level-4) |
| `0x2000` | PDPT (Page Directory Pointer Table) |
| `0x3000` | PD (Page Directory, 4 entries × 2MB) |

Identity map ช่วง **0x0 – 0x800000** (8 MB):
```nasm
; PML4[0] → PDPT
mov dword [0x1000], 0x2003   ; present + writable

; PDPT[0] → PD
mov dword [0x2000], 0x3003

; PD[0..3] → 2MB huge pages
mov dword [0x3000], 0x000083   ; 0x000000 + PS + P + W
mov dword [0x3008], 0x200083   ; 0x200000 + ...
mov dword [0x3010], 0x400083   ; 0x400000 + ...
mov dword [0x3018], 0x600083   ; 0x600000 + ...
```

#### 3. เปิด Long Mode
```nasm
; โหลด PML4 ใน CR3
mov eax, 0x1000
mov cr3, eax

; เปิด PAE ใน CR4
mov eax, cr4
or  eax, (1 << 5)    ; PAE bit
mov cr4, eax

; ตั้ง IA32_EFER.LME ผ่าน MSR
mov ecx, 0xC0000080  ; IA32_EFER
rdmsr
or  eax, (1 << 8)    ; LME
wrmsr

; ตั้ง CR0.PG = 1 (Paging) — ทำให้เข้า Long Mode จริง
mov eax, cr0
or  eax, (1 << 31)
mov cr0, eax

; Far jump ด้วย 64-bit GDT descriptor
jmp 0x08:long_mode_entry   ; CS = 64-bit code segment
```

#### 4. โหลด x64 Kernel ด้วย BIOS INT 0x13 (ยังอยู่ Real Mode)

Stage 3 ยังอยู่ใน Real Mode ขณะโหลด kernel จึงใช้ **BIOS INT 0x13** ได้:

```nasm
; ตรวจสอบ LBA Extensions (INT 0x13 AH=0x41)
mov ah, 0x41 / mov bx, 0x55AA / int 0x13
jc use_chs_kernel

; LBA mode: โหลด 12 chunks × 128 sectors = 1536 sectors (768 KB)
; จาก sector 2048 → temp buffer 0x10000
mov word [lba_cur], 2048    ; x64 kernel เริ่มที่ sector 2048
mov word [seg_cur], 0x1000  ; physical 0x10000
mov word [chunks],  12

.load_loop:
    ; INT 0x13 AH=0x42 Extended Read (disk_packet)
    mov ah, 0x42 / mov dl, 0x80 / int 0x13
    add word [lba_cur], 128
    add word [seg_cur], 0x1000  ; ขยับ 64 KB ต่อ chunk
    dec word [chunks]
    jnz .load_loop
; kernel อยู่ที่ 0x10000–0xBFFFF (768 KB) หลัง loop
```

#### 5. Copy Kernel ใน 64-bit Mode

หลังเข้า Long Mode ค่อย copy จาก temp buffer ไปที่ 1 MB:

```nasm
[BITS 64]
long_mode_start:
    mov ax, 0x10
    mov ds, ax  ; ds/es/fs/gs/ss = data selector
    mov rsp, 0x7E00

    ; Copy: 0x10000 → 0x100000 (1536 sectors = 768 KB)
    mov rsi, 0x10000
    mov rdi, 0x100000
    mov rcx, (1536 * 512) / 8  ; 98304 qwords
    cld
    rep movsq

    mov rax, 0x100000
    jmp rax  ; kernel_main()
```

---

## Memory Map หลัง Boot

| Address | เนื้อหา |
|---------|---------|
| `0x0000–0x03FF` | Real Mode IVT (BIOS) |
| `0x0400–0x04FF` | BIOS Data Area |
| `0x0500–0x7BFF` | Stack (Stage 1/2/3) |
| `0x7C00–0x7DFF` | Stage 1 code (MBR) |
| `0x8000–0x8FFF` | Stage 2 code |
| `0x9000–0x9FFF` | Stage 3 code |
| `0x1000–0x3FFF` | Page tables (PML4, PDPT, PD) |
| `0x10000–0xCFFFF` | Kernel temp load buffer (โหลดโดย Stage 2/3 ก่อน copy ไป 1 MB) |
| `0x100000` | Kernel binary entry point (`kernel_main`) |
| `0x500000` | Kernel stack top |
| `0xB8000` | VGA text buffer |

---

## ข้อควรระวัง

- **ขนาด kernel binary**: Bootloader รองรับ x64 kernel ได้สูงสุด **768 KB** (1536 sectors × 512 B, 12 chunks) และ x86 kernel **614 KB** (1200 sectors) ถ้า kernel ใหญ่กว่านี้ ต้องเพิ่มจำนวน sector/chunks ใน Stage 2/3
- **A20 Line**: ถ้า A20 ไม่เปิด memory address ที่ bit 20 จะถูก wrap → kernel crash แปลกๆ
- **Paging**: identity map แค่ 0–8 MB เท่านั้น ถ้า kernel เข้าถึงนอกช่วงนี้จะ Page Fault
