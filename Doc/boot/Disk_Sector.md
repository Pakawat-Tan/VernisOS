
# 🧠 Hard disk Sector

| Sector Range | Sectors (ขนาด) | ขนาดไฟล์ (Bytes)  | ข้อมูล                           | Memory Address (โหลดเข้า)        | หมายเหตุ                                         |
| ------------ | -------------- | ---------------- | ------------------------------ | ------------------------------- | ------------------------------------------------|
| 0            | 01             | 512              | Stage 1 Bootloader (Real Mode) | 0x7C00                          | BIOS โหลด sector แรกอัตโนมัติ                      |
| 1–5          | 05             | 2560             | x86 Stage 2 Bootloader         | 0x8000                          | โหลด kernel, ตรวจ arch                          |
| 6–11         | 06             | 3072             | x86-64 Stage 3 Bootloader      | 0x9000                          | เตรียม jump ไป Long Mode                         |
| 12–29        | 18             | 9216             | Kernel x86 (Protected Mode)    | 0x100000 (1MB)                  | ต้องเป็น flat binary หรือ ELF                      |
| 30–47        | 18             | 9216             | Kernel x86-64 (Long Mode)      | 0x100000 (1MB)                  | สำหรับ 64-bit                                    |
| 48–53        | 06             | 3072             | Bootloader ARM64               | 0x40000000                      | ต้องเผื่อขนาดจริงและจัดที่อยู่ให้ไม่ทับกับ kernel            |
| 54–72        | 19             | 9728             | Kernel ARM64                   | 0x40400000 (4MB)                | เช่น 0x40000000 + ขนาด bootloader                |
