# VernisOS — เอกสารภาพรวม

> เวอร์ชัน: 0.1.0-dev | อัปเดต: 2026-04-04

---

## VernisOS คืออะไร?

VernisOS เป็นระบบปฏิบัติการ (Operating System) แบบ **Microkernel** ที่พัฒนาขึ้นเพื่อการศึกษาและวิจัย
ทำงานบนสถาปัตยกรรม **x86 (32-bit)** และ **x86_64 (64-bit)** อย่างสมบูรณ์จาก disk image เดียว

เขียนด้วย 3 ภาษาหลัก:
- **Assembly (NASM)** — Bootloader และ CPU-specific stubs (interrupt, syscall)
- **C (GNU99)** — Kernel หลัก, driver, shell, filesystem, security layer
- **Rust (no_std)** — Scheduler, Memory allocator, AI engine ใน kernel

---

## สถาปัตยกรรมโดยรวม

```
┌─────────────────────────────────────────────────────────┐
│                  Python AI Engine (Host)                 │
│    ai_listener.py → auto_tuner → anomaly_detector       │
│                   ↕ COM2 UART (TCP 4444 ใน QEMU)        │
├─────────────────────────────────────────────────────────┤
│                    Kernel Space (Ring 0)                  │
│                                                          │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌────────┐  │
│  │  CLI     │  │ AI Bridge│  │ VernisFS │  │  IPC   │  │
│  │ (shell)  │  │ (COM2)   │  │ (ATA PIO)│  │(mailbox)│  │
│  └────┬─────┘  └────┬─────┘  └──────────┘  └────────┘  │
│       │             │                                    │
│  ┌────▼─────────────▼──────────────────────────────┐    │
│  │         Kernel Core (x86/x86_64 C layer)        │    │
│  │   GDT | IDT | PIC | PIT | UART | VGA | PS/2    │    │
│  └────────────────────┬────────────────────────────┘    │
│                        │                                 │
│  ┌─────────────────────▼──────────────────────────┐     │
│  │          Rust no_std staticlib                  │     │
│  │   Scheduler | Heap Allocator | AI Engine       │     │
│  │   Syscall Dispatch | Module Registry           │     │
│  └────────────────────────────────────────────────┘     │
│                                                          │
│  ┌───────────────────────────────────────────────┐      │
│  │  Security Layer                                │      │
│  │  Policy Enforce | Sandbox | UserDB | AuditLog │      │
│  └───────────────────────────────────────────────┘      │
├─────────────────────────────────────────────────────────┤
│                     Hardware (x86 / x86_64)              │
│         CPU | Disk (ATA PIO) | UART | VGA | PS/2        │
└─────────────────────────────────────────────────────────┘
```

---

## โครงสร้างไฟล์

```
VernisOS/
├── boot/x86/            # Bootloader (NASM)
│   ├── stage1.asm       # MBR — CPUID check, โหลด stage2
│   ├── stage2.asm       # โหลด stage3, ตรวจสอบ CPU mode
│   └── stage3.asm       # Long mode + paging, โหลด x64 kernel
│
├── kernel/
│   ├── arch/
│   │   ├── x86/             # 32-bit kernel
│   │   │   ├── kernel_x86.c # kernel_main() x86
│   │   │   ├── interrupts.asm
│   │   │   └── linker.ld
│   │   └── x86_64/          # 64-bit kernel
│   │       ├── kernel_x64.c # kernel_main() x64
│   │       ├── interrupts.asm
│   │       ├── syscall.asm  # SYSCALL/SYSRET stub
│   │       └── linker.ld
│   │
│   ├── core/verniskernel/   # Rust no_std staticlib
│   │   └── src/
│   │       ├── lib.rs        # Global allocator, init
│   │       ├── scheduler.rs  # Process scheduler
│   │       ├── memory.rs     # Memory helpers
│   │       ├── syscall.rs    # Syscall dispatch
│   │       └── ai.rs         # In-kernel AI engine
│   │
│   ├── drivers/
│   │   ├── ai_bridge.c      # COM2 AI protocol bridge
│   │   └── ai_engine.c      # AI engine C wrapper
│   ├── fs/
│   │   └── vernisfs.c       # Filesystem (ATA PIO)
│   ├── ipc/
│   │   └── ipc.c            # Message queue + channel IPC
│   ├── log/
│   │   └── klog.c           # Kernel structured log
│   ├── module/
│   │   └── module.c         # Dynamic module registry
│   ├── security/
│   │   ├── policy_enforce.c # Policy-based command check
│   │   ├── policy_loader.c  # Load VPOL binary from disk
│   │   ├── sandbox.c        # Capability-based sandbox
│   │   ├── sha256.c         # SHA-256 implementation
│   │   ├── auditlog.c       # Security audit log
│   │   └── userdb.c         # User authentication
│   ├── selftest/
│   │   └── selftest.c       # Boot-time self tests
│   └── shell/
│       └── cli.c            # Interactive CLI shell
│
├── include/                 # Header files ทั้งหมด
│
├── ai/                      # Python AI Engine (Host)
│   ├── ai_listener.py       # Main listener loop
│   ├── auto_tuner.py        # Kernel parameter tuning
│   ├── anomaly_detector.py  # Process anomaly detection
│   ├── policy_manager.py    # Policy management
│   ├── config/
│   │   ├── anomaly_rules.yaml
│   │   └── policy.yaml      # → compile → make/policy.bin
│   └── tools/
│       ├── policy_compile.py # YAML policy → VPOL binary
│       └── mkfs_vernis.py    # สร้าง VernisFS image
│
├── docs/                    # เอกสาร (ไฟล์นี้และเพื่อน)
├── Makefile
└── os.img                   # Disk image (4 MB)
```

---

## disk image layout

| Sector | Offset (bytes) | เนื้อหา |
|--------|---------------|---------|
| 0 | 0 | Stage 1 MBR (512 bytes) |
| 1–5 | 512 | Stage 2 (2,048 bytes padded) |
| 6–11 | 3,072 | Stage 3 (2,048 bytes padded) |
| 12–2047 | 6,144 | x86 kernel binary |
| 2048–4095 | 1,048,576 | x64 kernel binary |
| 4096 | 2,097,152 | Policy blob (VPOL) |
| 5120+ | 2,621,440 | VernisFS image |

---

## เอกสารแต่ละส่วน

| ไฟล์ | หัวข้อ |
|------|--------|
| [boot/BOOT.md](boot/BOOT.md) | Boot stages — Real Mode → Long Mode |
| [kernel/KERNEL.md](kernel/KERNEL.md) | Kernel architecture — init sequence, interrupt, syscall |
| [kernel/SCHEDULER.md](kernel/SCHEDULER.md) | Process scheduler — PCB, priority, FFI |
| [components/FILESYSTEM.md](components/FILESYSTEM.md) | VernisFS — disk layout, API, ATA PIO |
| [components/AI_BRIDGE.md](components/AI_BRIDGE.md) | AI Bridge — COM2 protocol, auto-tuning |
| [components/CLI.md](components/CLI.md) | CLI Shell — commands, readline, privilege |
| [kernel/IPC.md](kernel/IPC.md) | IPC — message queue, channel |
| [kernel/SECURITY.md](kernel/SECURITY.md) | Security — policy, sandbox, userdb, audit |
| [dev/BUILD.md](dev/BUILD.md) | Build system — Makefile, toolchain, flags |
| [STATUS.md](STATUS.md) | สิ่งที่มีและสิ่งที่ยังขาด + Roadmap |
