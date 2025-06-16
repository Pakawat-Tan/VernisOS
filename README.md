# VernisOS
I'm trying to develop an operating system to study and try to make the system faster and more stable.

# Modular OS with AI Core (AI-powered Microkernel OS)

## แนวความคิดในการพัฒนา

ระบบปฏิบัติการที่พัฒนาขึ้นนี้มีจุดมุ่งหมายเพื่อสร้าง **ระบบปฏิบัติการที่ยืดหยุ่นและรวดเร็ว** โดยการใช้ **สถาปัตยกรรม Microkernel** ซึ่งแบ่งออกเป็นหลายส่วนที่ทำงานแยกกันอย่างอิสระ เช่น Bootloader, Kernel, Modules, และ User Environment ซึ่งทั้งหมดสามารถใช้งานร่วมกันได้แบบ **modular** นอกจากนี้ ระบบยังสามารถติดตั้ง **AI Engine** ที่ฝังลงไปใน **Core System** เพื่อช่วยในการตรวจสอบ, ปรับแต่งระบบ, และช่วยวิเคราะห์ข้อมูลต่าง ๆ เช่น crash log และ process anomalies โดยจะไม่มี GUI ให้ใช้งาน แต่จะใช้ **CLI/Terminal** ในการติดต่อกับ AI ผ่านคำสั่งที่จำกัดตามสิทธิ์การเข้าถึง

## จุดประสงค์ของโปรเจค

1. **สร้าง OS แบบ modular** ที่สามารถแยกแต่ละส่วนออกจากกัน
2. **รวม AI Engine เข้ากับ Core System** เพื่อเพิ่มความสามารถในการตรวจสอบและปรับปรุงการทำงานของระบบ
3. **ระบบที่ปลอดภัยและยืดหยุ่น** โดยให้ผู้ใช้สามารถเข้าถึงได้ผ่าน CLI เท่านั้น
4. **พัฒนาโดยใช้ภาษา Assembly, C, และ Rust** เพื่อประสิทธิภาพและความเร็วในการทำงาน

## แผนการดำเนินงาน (Development Plan)

### 📅 ตารางพัฒนาระบบ

| **Phase** | **ระยะเวลา**  | **ส่วนที่พัฒนา**                                | **ภาษา/เทคโนโลยี**                  | **คำอธิบาย** 
|-----------|---------------|---------------------------------------------|------------------------------------|--------------------------------------------------------------------
| **1**     | สัปดาห์ 1–2     | วางสถาปัตยกรรมและเขียนเอกสารเบื้องต้น            | Markdown, Draw.io                  | สร้างโครงสร้างระบบ, model, interaction flow และ file layout  
| **2**     | สัปดาห์ 3–5     | Bootloader (BIOS/UEFI)                      | Assembly (NASM), C                 | สร้าง bootloader ให้สามารถโหลด kernel ได้จาก disk พร้อมตรวจ arch 
| **3**     | สัปดาห์ 6–8     | Core Kernel (Microkernel) + Arch Layer      | C, Rust                            | เขียน kernel สำหรับแต่ละ arch (x86, x86_64, AArch64) รองรับ syscall, memory, scheduling 
| **4**     | สัปดาห์ 9–10    | Inter-process Communication (IPC)           | C                                  | พัฒนา kernel IPC เพื่อใช้ติดต่อกับ modules และ AI 
| **5**     | สัปดาห์ 11–13   | Module Loader + Dynamic Linking             | C, Rust                            | ระบบโหลด/ถอด module runtime และระบบ permission 
| **6**     | สัปดาห์ 14–16   | User Sandbox Environment                    | C, Rust                            | แยก sandbox สำหรับ process users เพื่อความปลอดภัย 
| **7**     | สัปดาห์ 17–18   | CLI / Terminal System                       | C                                  | Shell เบื้องต้น, command handler, user session 
| **8**     | สัปดาห์ 19–21   | AI IPC Bridge (Kernel ↔ AI Engine)          | C (kernel side), Python (listener) | สร้างระบบ socket หรือ memory-pipe ระหว่าง kernel กับ AI Python Engine 
| **9**     | สัปดาห์ 22–24   | Python AI Engine: Corelib + Listener        | Python 3.12                        | สร้าง corelib, ai_listener, policy manager สำหรับสื่อสารกับ kernel 
| **10**    | สัปดาห์ 25–27   | AI Behavior Monitor                         | Python                             | ตรวจสอบ process/module anomalies ตาม policy 
| **11**    | สัปดาห์ 28–29   | AI Auto-Tuning Engine                       | Python                             | ปรับ memory/scheduler ตาม load (เริ่มต้นง่ายก่อน) 
| **12**    | สัปดาห์ 30–32   | Policy System + YAML Config Loader          | Python (ruamel.yaml)               | โหลด policy จาก YAML เพื่อควบคุม AI ได้แบบปลอดภัย 
| **13**    | สัปดาห์ 33–35   | Kernel AI Policy Enforcement Layer          | C                                  | ฝั่ง kernel ตรวจสอบสิทธิ์ก่อนส่งคำสั่งไป AI 
| **14**    | สัปดาห์ 36–38   | Test CLI-AI Interaction + Permission System | C, Python                          | ทดสอบว่า CLI สามารถใช้ AI ได้ตามระดับสิทธิ์ (root, admin, user) 
| **15**    | สัปดาห์ 39–42   | Optimize kernel, module และ sandbox         | Rust, C                            | ปรับประสิทธิภาพ memory, context switch, load balancing
| **16**    | สัปดาห์ 43–45   | Integration Test & Logging System           | C, Python                          | เชื่อมทุกส่วนเข้าด้วยกัน และสร้างระบบ debug log
| **17**    | สัปดาห์ 46–48   | Prepare Developer Preview                   | Makefile, Doc tools                | ทำ doc, สร้าง build script, prepare dev release

## แนวทางการพัฒนา

1. **การพัฒนา Bootloader และ Kernel:**
   - ใช้ **Assembly** และ **C** เพื่อให้การทำงานของระบบเริ่มต้นได้เร็วที่สุดและรองรับสถาปัตยกรรมต่าง ๆ
   - ในการพัฒนา kernel, เราจะใช้ **C** และ **Rust** เพื่อประสิทธิภาพและความปลอดภัยที่สูง

2. **การพัฒนา AI Engine:**
   - ใช้ **Python 3.12** สำหรับพัฒนา AI Engine ซึ่งจะประกอบด้วยหลายโมดูล ได้แก่ **Behavior Monitor**, **Auto-Tuning Engine**, และ **Crash Log Analyzer** ที่สามารถสื่อสารกับ Kernel ผ่าน **IPC** หรือ **Socket**
   - AI จะใช้ **corelib.py** เป็น interface กับ kernel และจะทำงานภายใต้ข้อกำหนดที่จัดทำใน **policy.yaml**

3. **การพัฒนาโมดูลต่าง ๆ:**
   - ระบบรองรับการโหลดโมดูลต่าง ๆ ที่ไม่ได้เกี่ยวข้องกับการทำงานหลักของ OS (เช่น file system, network stack, device drivers)
   - การพัฒนาโมดูลจะทำใน **C** และ **Rust** สำหรับการจัดการ resource และประสิทธิภาพการทำงาน

4. **การจัดการสิทธิ์และการควบคุมการเข้าถึง:**
   - ระบบจะให้สิทธิ์ต่าง ๆ แก่ผู้ใช้ผ่าน **CLI** และมีการควบคุมสิทธิ์ที่แม่นยำผ่านการตรวจสอบจาก **AI** ก่อนที่จะให้สามารถใช้งานบางคำสั่งที่มีผลต่อระบบ

## วิธีการใช้

1. **การเริ่มต้นใช้งาน:**
   - โหลดโปรเจคและสร้าง environment สำหรับการพัฒนาด้วยคำสั่ง `make setup`
   - สามารถทดสอบการทำงานของระบบผ่าน **CLI** โดยใช้คำสั่งต่าง ๆ เช่น `ai_status`, `mod_control`, และ `sys_monitor`
   - การติดต่อกับ AI ผ่าน CLI จะถูกจำกัดตามสิทธิ์ที่กำหนดใน `ai_policy.yaml`

---

ใน README.md นี้จะครอบคลุมทั้งแผนการพัฒนา, แนวทางการพัฒนา และรายละเอียดของโครงสร้างการทำงานของระบบทั้งหมด ทำให้สามารถเข้าใจการพัฒนาได้ชัดเจนและเป็นแนวทางที่ดีสำหรับการเริ่มต้นโปรเจค

หากคุณต้องการให้เพิ่มเติมในส่วนไหน หรือปรับรายละเอียดต่าง ๆ เพิ่มเติม ผมยินดีช่วยเสมอครับ
