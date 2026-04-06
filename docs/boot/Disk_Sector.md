
# 🧠 Hard disk Sector

| Sector Range  | Sectors | ขนาดไฟล์ (Bytes) | ข้อมูล                          | Memory Address (โหลดเข้า) | หมายเหตุ                                                       |
| ------------- | ------- | ---------------- | ------------------------------- | ------------------------- | -------------------------------------------------------------- |
| 0             | 1       | 512              | Stage 1 Bootloader (Real Mode)  | 0x7C00                    | BIOS โหลด sector แรกอัตโนมัติ                                   |
| 1–5           | 5       | 2,048 (padded)   | Stage 2 Bootloader              | 0x8000                    | ตรวจ cpu_mode, โหลด Stage 3 หรือ x86 kernel                    |
| 6–11          | 6       | 2,048 (padded)   | Stage 3 Bootloader (x86_64)     | 0x9000                    | Setup Long Mode, paging, โหลด x64 kernel                       |
| 12–1211       | 1,200   | 614,400          | Kernel x86 (Protected Mode)     | 0x10000 → copy → 0x100000 | โหลดเป็น chunks (9×128 + 48), copy ไป 1 MB                     |
| 2048–3583     | 1,536   | 786,432          | Kernel x64 (Long Mode)          | 0x10000 → copy → 0x100000 | โหลดเป็น chunks (12×128), copy ไป 1 MB ใน 64-bit mode          |
| 4096          | ~2      | ~804             | Policy Blob (VPOL)              | ไม่ใส่ RAM ตรงๆ            | `policy_load_from_disk()` อ่านตรงจาก sector นี้                |
| 5120+         | ~7      | ~3,584           | VernisFS Filesystem Image       | ไม่ใส่ RAM ตรงๆ            | `vfs_init()` อ่าน superblock จาก sector 5120                   |

## หมายเหตุ

- **x86 kernel** ถูกโหลดที่ temporary buffer `0x10000` ก่อน แล้ว copy ไป `0x100000` ทั้งใน Stage 2 (32-bit path) และ Stage 3 (fallback)
- **x64 kernel** ถูกโหลดที่ `0x10000` ใน Real Mode แล้ว copy ไป `0x100000` หลังเข้า 64-bit Long Mode
- **Sector 2048** เริ่มต้น x64 kernel ให้ห่างจาก x86 kernel (sectors 12–1211) เพื่อป้องกัน overlap
- **ARM64** ไม่ได้รับการรองรับในปัจจุบัน
- ขนาด image รวม: **4 MB** (`dd if=/dev/zero of=os.img bs=1M count=4`)
