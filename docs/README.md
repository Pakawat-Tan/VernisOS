# 📚 VernisOS — เอกสาร

> **เวอร์ชัน**: 0.1.0-dev | **ภาษา**: Assembly, C, Rust, Python

---

## 📁 โครงสร้างเอกสาร

```
docs/
├── README.md              ← ไฟล์นี้ (สารบัญ)
├── OVERVIEW.md            ← ภาพรวมระบบ + file tree + disk layout
├── STATUS.md              ← สถานะ Phase + สิ่งที่ยังขาด + Roadmap
│
├── boot/                  ← ระบบบูต (Bootloader)
│   ├── BOOT.md            ← 3-stage boot, Real Mode → Long Mode
│   ├── Address.md         ← ตาราง Memory Address ระบบบูต
│   ├── Architecture.md    ← สถาปัตยกรรมระบบบูต
│   └── Disk_Sector.md     ← ตาราง Disk Sector layout
│
├── kernel/                ← Kernel internals
│   ├── KERNEL.md          ← Init sequence, GDT/IDT, interrupt, syscall
│   ├── SCHEDULER.md       ← Process scheduler, PCB, priority, Rust FFI
│   ├── IPC.md             ← Message queue + Channel, syscall 20–27
│   └── SECURITY.md        ← Policy, sandbox, userdb, audit log
│
├── components/            ← ส่วนประกอบหลัก
│   ├── FILESYSTEM.md      ← VernisFS, ATA PIO, disk layout, API
│   ├── CLI.md             ← Shell, คำสั่ง 24 รายการ, privilege level
│   └── AI_BRIDGE.md       ← COM2 protocol, AI auto-tuning, Python bridge
│
└── dev/                   ← สำหรับนักพัฒนา
    ├── BUILD.md           ← Build system, Makefile targets, compiler flags
    ├── ARCHITECTURE.md    ← ภาพรวมสถาปัตยกรรม (English)
    └── phases/
        ├── README.md      ← ตารางสรุปทุก Phase
        ├── PHASE01.md     ← สถาปัตยกรรมและเอกสาร
        ├── PHASE02.md     ← Bootloader (BIOS 3-stage)
        ├── PHASE03.md     ← Core Kernel + Arch Layer
        ├── PHASE04.md     ← IPC
        ├── PHASE05.md     ← Module Loader
        ├── PHASE06.md     ← User Sandbox
        ├── PHASE07.md     ← CLI / Terminal
        ├── PHASE08.md     ← AI IPC Bridge
        ├── PHASE09.md     ← Python AI Engine
        ├── PHASE10.md     ← AI Behavior Monitor
        ├── PHASE11.md     ← AI Auto-Tuning
        ├── PHASE12.md     ← Policy System
        ├── PHASE13.md     ← Kernel Auth + Enforcement
        ├── PHASE14.md     ← Testing + Permission
        ├── PHASE15.md     ← Optimization
        ├── PHASE16.md     ← Logging + Integration Test
        └── PHASE17.md     ← Developer Preview
```

---

## ⚡ เริ่มต้นเร็ว

```bash
# build ทุกอย่าง
make rust && make

# รัน x64 ใน QEMU
make run64

# debug ผ่าน GDB
make debug64
# (terminal อื่น) x86_64-elf-gdb make/kernel/arch/x86_64/kernel_x64.elf
```

---

## 🗂 หมวดหมู่เอกสาร

### ระบบบูต
| เอกสาร | เนื้อหา |
|--------|---------|
| [boot/BOOT.md](boot/BOOT.md) | 3-stage bootloader, BIOS INT 0x13, Long Mode setup |
| [boot/Address.md](boot/Address.md) | Memory address แต่ละ stage |
| [boot/Architecture.md](boot/Architecture.md) | สถาปัตยกรรมการบูต |
| [boot/Disk_Sector.md](boot/Disk_Sector.md) | ตำแหน่ง sector บน disk image |

### Kernel
| เอกสาร | เนื้อหา |
|--------|---------|
| [kernel/KERNEL.md](kernel/KERNEL.md) | Init sequence 26 ขั้นตอน, GDT/IDT, interrupt, syscall |
| [kernel/SCHEDULER.md](kernel/SCHEDULER.md) | PCB, priority, preemptive scheduling, Rust FFI |
| [kernel/IPC.md](kernel/IPC.md) | Message queue, channel, syscall 20–27 |
| [kernel/SECURITY.md](kernel/SECURITY.md) | Policy, sandbox, SHA-256 auth, audit log |

### ส่วนประกอบ
| เอกสาร | เนื้อหา |
|--------|---------|
| [components/FILESYSTEM.md](components/FILESYSTEM.md) | VernisFS, ATA PIO, superblock, file API |
| [components/CLI.md](components/CLI.md) | Shell, 24 commands, privilege 0/50/100 |
| [components/AI_BRIDGE.md](components/AI_BRIDGE.md) | COM2 UART, protocol frames, auto-tuner |

### Developer
| เอกสาร | เนื้อหา |
|--------|---------|
| [dev/BUILD.md](dev/BUILD.md) | Prerequisites, make targets, compiler flags, troubleshooting |
| [dev/ARCHITECTURE.md](dev/ARCHITECTURE.md) | Architecture overview (English) |
| [dev/phases/README.md](dev/phases/README.md) | ตารางสรุปทุก 17 Phase |
| [dev/phases/PHASE01.md](dev/phases/PHASE01.md) — [PHASE17.md](dev/phases/PHASE17.md) | เอกสารแต่ละ Phase (ภาษาไทย, ละเอียด) |

### ภาพรวม / สถานะ
| เอกสาร | เนื้อหา |
|--------|---------|
| [OVERVIEW.md](OVERVIEW.md) | ภาพรวมระบบ, file tree, disk image layout |
| [STATUS.md](STATUS.md) | 20 สิ่งที่มี, 20 สิ่งที่ยังขาด, Roadmap Phase 16–22 |
