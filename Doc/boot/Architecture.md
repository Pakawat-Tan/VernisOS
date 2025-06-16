# 🧠 CPU Architecture Detection & Boot Flow (Stage-based Design)

## 🔍 Overview

Bootloader ของระบบนี้มีโครงสร้างแบบ 3-stage เพื่อรองรับหลายสถาปัตยกรรมทั้ง x86, x86_64 และ ARM โดยมีการตรวจสอบ CPUID เพื่อแยก flow ของการบูตอย่างเหมาะสมกับ CPU ที่ตรวจพบ

---

## 🪜 Boot Flow Summary

### Stage 1: Initial Bootloader (`@ 0x7C00`)

- ตรวจสอบว่า CPU รองรับ `CPUID` หรือไม่
  - ❌ ไม่รองรับ → ถือว่าเป็น ARM → Jump ไป `0xA000`
  - ✅ รองรับ → ตรวจสอบ Long Mode:
    - ✅ รองรับ Long Mode → `cpu_mode = 1 (x86_64)` → โหลด Stage 3 ที่ `0x90000` → `jmp 0x9000`
    - ❌ ไม่รองรับ → `cpu_mode = 2 (x86)` → โหลด Stage 2 ที่ `0x8000` → `jmp 0x8000`

---

### Stage 2: Protected Mode Setup (`@ 0x8000`)

- อ่าน `cpu_mode` จาก `0x7FF0`
- กรณี:
  - `1` → เตรียมเข้า 64-bit Mode:
    - โหลด Stage 3 (`0x90000`) จาก LBA sector 32
    - เปิด A20 Line → Jump ไป `0x9000`
  - `2` → เตรียม Protected Mode (x86 32-bit):
    - โหลด Kernel จาก LBA sector 8 → `0x10000`
    - ตั้ง GDT, เปิด CR0 → Jump ไป `protected_mode_entry`
  - อื่นๆ → แสดง "Unsupported Architecture" → Halt

---

### Stage 3: Long Mode Setup (`@ 0x9000`)

- ตรวจสอบ A20 Line → เปิดหากยังไม่เปิด
- โหลด Kernel (ใช้ LBA หรือ fallback เป็น CHS)
- สร้าง Paging Structures (PML4, PDPT, PD)
- เปิด Long Mode:
  - `CR4.PAE = 1`
  - `EFER.LME = 1`
  - `CR0 |= PG | PE`
- Jump ไป `0x100000` → เข้าสู่ 64-bit Kernel

---

## 🔧 Detailed Boot Decision Table

| Stage | ที่อยู่   | จุดทำงานหลัก                                                      |
|-------|----------|---------------------------------------------------------------------|
| 1     | 0x7C00   | ตรวจ CPUID → ตัดสินใจเข้า Stage 2 หรือ Stage 3 หรือ ARM             |
| 2     | 0x8000   | เตรียม Protected Mode หรือ Long Mode และโหลด Kernel ที่เหมาะสม       |
| 3     | 0x9000   | ตั้งค่า Paging + GDT → เข้าสู่ Long Mode (64-bit) และเริ่ม Kernel     |

---

## 📝 หมายเหตุ

- ใช้ `INT 13h AH=42h` สำหรับอ่าน Disk แบบ LBA (fallback เป็น CHS)
- เปิด A20 Line ด้วย Fast A20 (`port 0x92`)
- ใช้ BIOS `INT 10h` สำหรับแสดงข้อความ
- GDT ถูกจัดเตรียมให้เล็กและเหมาะกับโหมดที่เปลี่ยน (PM/LM)

---

## 🔗 สถาปัตยกรรมที่รองรับ

- ✅ x86 (Real Mode → Protected Mode)
- ✅ x86_64 (Real Mode → Protected Mode → Long Mode)
- 🧪 ARM / AArch64 (อยู่ในแผนรองรับ โดยใช้ fallback หาก CPUID ไม่มี)

---

## 📌 ภาพรวม Flow สั้น

```plaintext
Stage 1 (0x7C00)
 ├── ถ้า CPUID ไม่มี → ARM → jmp 0xA000
 └── ถ้า CPUID มี
      ├── ถ้ามี Long Mode → cpu_mode=1 → Stage 3 @ 0x90000
      └── ถ้าไม่มี Long Mode → cpu_mode=2 → Stage 2 @ 0x8000

Stage 2 (0x8000) ← x86 (32-bit)
 └── เตรียม GDT → เปิด PE → โหลด Kernel @ 0x100000 → jmp EIP=0x100000

Stage 3 (0x9000) ← x86_64
 └── สร้าง PML4 → เปิด CR4.PAE, CR0.PG, EFER.LME → jmp RIP=0x100000
