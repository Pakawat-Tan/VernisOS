## 🧠 CPU Architecture Detection & Boot Flow (Stage-based Design)

### 🔍 Logic Diagram

==========================================================================================
|  Stage 1 Bootloader (Real Mode @ 0x7C00)                                               |
| ตรวจสอบว่า CPU รองรับ CPUID instruction หรือไม่                                            |
| ├── [Yes] → เรียกใช้งาน CPUID function                                                   |
| │     ├── ตรวจสอบ CPU Architecture (32-bit หรือ 64-bit)                                 |
| │     │     ├── [Long Mode Available] → โหลดและเริ่ม Stage 3 (x86_64)                    |
| │     │     └── [No Long Mode]        → โหลดและเริ่ม Kernel 32-bit (x86)                 |
| │     └── (สามารถเพิ่มการตรวจสอบ feature อื่น ๆ เช่น virtualization, SSE, etc.)             |
| └── [No] → สมมุติว่าเป็น non-x86 architecture → โหลด ARM Kernel (AArch32 หรือ AArch64)      |
==========================================================================================

====================================================================================
|  Stage 2 Bootloader (Real Mode @ 0x8000)                                         |
|                                                                                  |
|  ┌── ตั้งค่า Segment Register, Stack และ Clear หน้าจอ                                |
|  ├── อ่านค่า cpu_mode ที่ Stage 1 ส่งมาไว้ที่ [0x7FF0]                                   |
|  │     ├── 1 → รองรับ Long Mode (x86_64)                                          |
|  │     ├── 2 → รองรับ Protected Mode เท่านั้น (x86 32-bit)                           |
|  │     └── อื่นๆ → Unsupported Architecture (อาจเป็น ARM)                           |
|  ┌── ตรวจสอบ cpu_mode                                                            |
|  ├── [1] → เตรียมโหมด 64-bit                                                      |
|  │     ├── แสดงข้อความเตรียมเข้า 64-bit                                             |
|  │     ├── โหลด Stage 3 จาก LBA sector 32 → 0x90000                              |
|  │     │     ├── ใช้ BIOS LBA (INT 13h AH=42h) ถ้าไม่ได้ fallback เป็น CHS            |
|  │     ├── เปิด A20 Line                                                          |
|  │     └── Jump ไป Stage 3 ที่ 0x0000:0x9000                                       |
|  ├── [2] → เตรียมโหมด Protected Mode (32-bit)                                     |
|  │     ├── แสดงข้อความเตรียมเข้า 32-bit                                             |
|  │     ├── โหลด Kernel จาก LBA sector 8 → 0x10000                                |
|  │     │     ├── ใช้ BIOS LBA (INT 13h AH=42h) ถ้าไม่ได้ fallback เป็น CHS            |
|  │     ├── เปิด A20 Line                                                          |
|  │     └── Jump ไป setup_protected_mode                                          |
|  │            ├── Load GDT                                                       |
|  │            ├── เปิด CR0 PE-bit                                                 |
|  │            └── Far jump เข้า protected_mode_entry                              |
|  └── [อื่น ๆ] → แสดงข้อความ "Unsupported Architecture" และ Halt                     |
|                                                                                  |
|  หมายเหตุเพิ่มเติม:                                                                  |
|    - ใช้ BIOS Function: INT 10h สำหรับข้อความ/หน้าจอ                                 |
|    - ใช้ Fast A20 (Port 0x92) สำหรับเปิด A20 Line                                   |
|    - ใช้ GDT ขนาดเล็กสำหรับ Protected Mode                                          |
====================================================================================

===================================================================================================================
|  Stage 3 Bootloader (Real Mode @ 0x9000)                                                                        |
|  เริ่มต้น bootloader (real mode)                                                                                   |
|  ├── เคลียร์ Segment Registers และ Stack Pointer                                                                  |
|  ├── แสดงข้อความ "Stage 3 - 64-bit Kernel Loader" ผ่าน BIOS INT 10h                                               |
|  └── ตรวจสอบว่า A20 line ถูกเปิดใช้งานหรือยัง                                                                         |
|        ├── [Enabled] → ดำเนินการต่อ                                                                               |
|        └── [Disabled] → พยายามเปิด A20 หากยังล้มเหลว → พิมพ์ “A20 Error” แล้ว halt                                    |
|  โหลด kernel (โดยใช้ LBA หาก BIOS รองรับ, ไม่งั้น fallback เป็น CHS)                                                  |
|  ├── [INT 13h Extensions Available] → โหลด kernel_x64 จาก sector 6 เป็นต้นไป                                      |
|  └── [Fallback to CHS] → โหลด kernel แบบ CHS จาก cylinder/head/sector ที่กำหนดไว้                                  |
|        ├── [Success] → ไปต่อ                                                                                     |
|        └── [Fail] → แสดง “Disk read error!” แล้ว halt                                                            |
|  เตรียมระบบ Memory Paging สำหรับ Long Mode                                                                        |
|  ├── Clear page table memory ที่ 0x1000, 0x2000, 0x3000                                                           |
|  ├── สร้าง PML4, PDPT, PD สำหรับ map 0x00000000–0x00200000 ด้วย 2MB page                                           |
|  ├── เปิด PAE (CR4.PAE = 1)                                                                                      |
|  └── เปิด Long Mode ผ่าน MSR EFER (bit LME)                                                                       |
|  สร้าง GDT ใหม่, เปิด Protected Mode และ Paging (CR0 |= PG|PE)                                                     |
|  └── Far Jump ไปยัง long_mode_start เพื่อเข้าสู่ 64-bit mode                                                          |
|  [64-bit Mode]                                                                                                  |
|  ├── Set segment registers (DS, SS = 0)                                                                         |
|  ├── เคลียร์หน้าจอ, แสดง “Entering 64-bit mode...”                                                                 |
|  ├── Copy kernel (โหลดไว้ที่ 0x10000) ไปยัง 0x100000                                                                |
|  └── Jump ไปยัง entry point ที่ 0x100000                                                                           |
===================================================================================================================

Stage 1 (0x7C00)  ← [Boot Sector loaded by BIOS]
├─ ตรวจสอบว่า CPU รองรับ CPUID หรือไม่
│
├─ ถ้า "มี" CPUID:
│   ├─ ตรวจสอบ Long Mode Support (ผ่าน CPUID EAX=80000001h → EDX[29])
│   ├─ ถ้า "รองรับ Long Mode":
│   │   ├─ เซต cpu_mode = 1 (x86_64)
│   │   └─ โหลด Stage 3 ที่ 0x90000 → `jmp 0x0000:0x9000`
│   ├─ ถ้า "ไม่รองรับ Long Mode":
│   │   ├─ เซต cpu_mode = 2 (x86)
│   │   └─ โหลด Stage 2 (x86 protected mode) ที่ 0x8000 → `jmp 0x0000:0x8000`
│
└─ ถ้า "ไม่มี" CPUID:
    └─ สมมุติว่าเป็น ARM → โหลด Stage 2 (ARM path) ที่ 0xA000 → `jmp 0x0000:0xA000`

Stage 2 (0x8000)  ← [สำหรับ x86 (Protected Mode)]
├─ เปิดใช้งาน A20 Line
├─ ตั้งค่า Global Descriptor Table (GDT)
├─ สลับจาก Real Mode → Protected Mode
├─ โหลด Kernel (32-bit) ไปยัง 0x100000
└─ กระโดดไปยัง Kernel Entry (EIP = 0x100000)

Stage 3 (0x9000)  ← [สำหรับ x86_64 (Long Mode)]
├─ เปิดใช้งาน A20 Line
├─ ตั้งค่า GDT สำหรับ 64-bit Mode
├─ สร้าง Paging Structures (PML4 → PDPT → PD) แบบ Identity Mapping
├─ เปิดใช้งาน:
│   ├─ CR4.PAE (Physical Address Extension)
│   ├─ CR0.PG (Paging)
│   ├─ MSR EFER.LME (Long Mode Enable)
│   └─ โหลด CR3 → ชี้ไปยัง PML4 Table
├─ เข้าสู่ Long Mode (64-bit)
├─ โหลด Kernel (ELF/Binary) ไปยัง 0x100000
└─ กระโดดไปยัง Kernel Entry (RIP = 0x100000)

