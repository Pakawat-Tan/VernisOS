# VernisOS (Modular OS with AI Core)

> **Version 0.1.0-dev** — A bare-metal Microkernel OS with an in-kernel Rust AI engine.  
> Supports x86 and x86_64 from a single disk image.

## Quick Start

```bash
make prerequisites    # Check toolchain
make                  # Build everything
make run64            # Run in QEMU (x64)
```

📖 **[Getting Started Guide](GETTING_STARTED.md)** — Full setup instructions

## Features

- **Dual-architecture**: x86 (32-bit) and x86_64 (64-bit) from one boot image
- **3-stage bootloader**: Runtime CPUID detection, automatic arch selection
- **Microkernel**: Scheduler, IPC (queues + channels), module loader, sandbox
- **In-kernel AI Engine** (Rust `no_std`): anomaly detection, auto-tuning, trust scoring
- **Policy system**: YAML → binary VPOL → runtime enforcement
- **VernisFS**: Minimal sector-based filesystem with user authentication
- **CLI with 18 commands**: Privilege-based access control (root/admin/user)
- **Structured logging**: klog (kernel) + audit log (security events)

## Documentation

| Document | Description |
|----------|-------------|
| [GETTING_STARTED.md](GETTING_STARTED.md) | Prerequisites, build, first boot |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture overview |
| [docs/BUILD.md](docs/BUILD.md) | Build system & Makefile targets |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Development guidelines |
| [CHANGELOG.md](CHANGELOG.md) | Version history (all phases) |

## แนวความคิดในการพัฒนา

ระบบปฏิบัติการที่พัฒนาขึ้นนี้มีจุดมุ่งหมายเพื่อสร้าง **ระบบปฏิบัติการที่ยืดหยุ่นและรวดเร็ว** โดยการใช้ **สถาปัตยกรรม Microkernel** ซึ่งแบ่งออกเป็นหลายส่วนที่ทำงานแยกกันอย่างอิสระ เช่น Bootloader, Kernel, Modules, และ User Environment ซึ่งทั้งหมดสามารถใช้งานร่วมกันได้แบบ **modular** นอกจากนี้ ระบบยังสามารถติดตั้ง **AI Engine** ที่ฝังลงไปใน **Core System** เพื่อช่วยในการตรวจสอบ, ปรับแต่งระบบ, และช่วยวิเคราะห์ข้อมูลต่าง ๆ เช่น crash log และ process anomalies โดยจะไม่มี GUI ให้ใช้งาน แต่จะใช้ **CLI/Terminal** ในการติดต่อกับ AI ผ่านคำสั่งที่จำกัดตามสิทธิ์การเข้าถึง

## จุดประสงค์ของโปรเจค

1. **สร้าง OS แบบ modular** ที่สามารถแยกแต่ละส่วนออกจากกัน
2. **รวม AI Engine เข้ากับ Core System** เพื่อเพิ่มความสามารถในการตรวจสอบและปรับปรุงการทำงานของระบบ
3. **ระบบที่ปลอดภัยและยืดหยุ่น** โดยให้ผู้ใช้สามารถเข้าถึงได้ผ่าน CLI เท่านั้น
4. **พัฒนาโดยใช้ภาษา Assembly, C, และ Rust** เพื่อประสิทธิภาพและความเร็วในการทำงาน

## แผนการดำเนินงาน (Development Plan)

### 📅 ตารางพัฒนาระบบ

| **Phase** | **สถานะ** | **ระยะเวลา**  | **ส่วนที่พัฒนา**                                | **ภาษา/เทคโนโลยี**                  | **คำอธิบาย** 
|-----------|----------|---------------|---------------------------------------------|------------------------------------|--------------------------------------------------------------------
| **1**     | ✅ Done  | สัปดาห์ 1–2     | วางสถาปัตยกรรมและเขียนเอกสารเบื้องต้น            | Markdown, Draw.io                  | สร้างโครงสร้างระบบ, model, interaction flow และ file layout  
| **2**     | ✅ Done  | สัปดาห์ 3–5     | Bootloader (BIOS/UEFI)                      | Assembly (NASM), C                 | สร้าง bootloader ให้สามารถโหลด kernel ได้จาก disk พร้อมตรวจ arch 
| **3**     | ✅ Done  | สัปดาห์ 6–8     | Core Kernel (Microkernel) + Arch Layer      | C, Rust                            | เขียน kernel สำหรับแต่ละ arch (x86, x86_64, AArch64) รองรับ syscall, memory, scheduling 
| **4**     | ✅ Done  | สัปดาห์ 9–10    | Inter-process Communication (IPC)           | C                                  | พัฒนา kernel IPC เพื่อใช้ติดต่อกับ modules และ AI 
| **5**     | ✅ Done  | สัปดาห์ 11–13   | Module Loader + Dynamic Linking             | C, Rust                            | ระบบโหลด/ถอด module runtime และระบบ permission 
| **6**     | ✅ Done  | สัปดาห์ 14–16   | User Sandbox Environment                    | C, Rust                            | แยก sandbox สำหรับ process users เพื่อความปลอดภัย 
| **7**     | ✅ Done  | สัปดาห์ 17–18   | CLI / Terminal System                       | C                                  | Shell เบื้องต้น, command handler, user session 
| **8**     | ✅ Done  | สัปดาห์ 19–21   | AI IPC Bridge (Kernel ↔ AI Engine)          | C (kernel side), Python (listener) | สร้างระบบ socket หรือ memory-pipe ระหว่าง kernel กับ AI Python Engine 
| **9**     | ✅ Done  | สัปดาห์ 22–24   | Python AI Engine: Corelib + Listener        | Python 3.xx                        | สร้าง corelib, ai_listener, policy manager สำหรับสื่อสารกับ kernel 
| **10**    | ✅ Done  | สัปดาห์ 25–27   | AI Behavior Monitor                         | Rust (in-kernel), C                | ตรวจสอบ process/module anomalies — ported เป็น Rust no_std ใน kernel
| **11**    | ✅ Done  | สัปดาห์ 28–29   | AI Auto-Tuning Engine                       | Rust (in-kernel), C                | ปรับ scheduler quantum ตาม load — รวมอยู่ใน in-kernel AI engine
| **12**    | ✅ Done  | สัปดาห์ 30–32   | Policy System + Config Loader               | Python, C, Rust                    | YAML config → binary blob → ฝังใน os.img → kernel โหลดตอน boot + COM2 hot-reload 
| **13**    | ✅ Done  | สัปดาห์ 33–35   | Kernel AI Policy Enforcement + VernisFS + Auth | C, Rust                         | VernisFS filesystem, SHA-256 auth, user DB, policy enforcement, login/su/logout CLI 
| **14**    | ✅ Done  | สัปดาห์ 36–38   | Test CLI-AI Interaction + Permission System | C, Python                          | Self-test, audit log, QEMU test harness, privilege verification 
| **15**    | ✅        | สัปดาห์ 39–42   | Optimize kernel, module และ sandbox         | Rust, C                            | ปรับประสิทธิภาพ memory, context switch, load balancing
| **16**    | ✅        | สัปดาห์ 43–45   | Integration Test & Logging System           | C, Python                          | เชื่อมทุกส่วนเข้าด้วยกัน และสร้างระบบ debug log
| **17**    | ✅        | สัปดาห์ 46–48   | Prepare Developer Preview                   | Makefile, Doc tools                | ทำ doc, สร้าง build script, prepare dev release

## แนวทางการพัฒนา

1. **การพัฒนา Bootloader และ Kernel:**
   - ใช้ **Assembly** และ **C** เพื่อให้การทำงานของระบบเริ่มต้นได้เร็วที่สุดและรองรับสถาปัตยกรรมต่าง ๆ
   - ในการพัฒนา kernel, เราจะใช้ **C** และ **Rust** เพื่อประสิทธิภาพและความปลอดภัยที่สูง

2. **การพัฒนา AI Engine:**
   - AI Engine ถูก port เป็น **Rust no_std** ทำงานเป็น in-kernel service module โดยตรง — ไม่ต้องพึ่ง Python หรือ COM2 serial
   - ประกอบด้วย: **EventStore** (ring buffer), **AnomalyDetector** (rate/pattern/threshold), **ProcessTracker** (per-PID trust), **AutoTuner** (load + quantum), **AlertDeduplicator**, **ResponseHandler**
   - C integration layer (`ai_engine.c`) เรียก Rust FFI → callbacks กลับไปปรับ scheduler quantum/priority
   - Python AI engine (`ai/`) ยังคงอยู่สำหรับการพัฒนา/ทดสอบภายนอก แต่ primary engine อยู่ใน kernel

3. **การพัฒนาโมดูลต่าง ๆ:**
   - ระบบรองรับการโหลดโมดูลต่าง ๆ ที่ไม่ได้เกี่ยวข้องกับการทำงานหลักของ OS (เช่น file system, network stack, device drivers)
   - การพัฒนาโมดูลจะทำใน **C** และ **Rust** สำหรับการจัดการ resource และประสิทธิภาพการทำงาน

4. **การจัดการสิทธิ์และการควบคุมการเข้าถึง:**
   - ระบบจะให้สิทธิ์ต่าง ๆ แก่ผู้ใช้ผ่าน **CLI** และมีการควบคุมสิทธิ์ที่แม่นยำผ่านการตรวจสอบจาก **AI** ก่อนที่จะให้สามารถใช้งานบางคำสั่งที่มีผลต่อระบบ

ใน README.md นี้จะครอบคลุมทั้งแผนการพัฒนา, แนวทางการพัฒนา และรายละเอียดของโครงสร้างการทำงานของระบบทั้งหมด ทำให้สามารถเข้าใจการพัฒนาได้ชัดเจนและเป็นแนวทางที่ดีสำหรับการเริ่มต้นโปรเจค