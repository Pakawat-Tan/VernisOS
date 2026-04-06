# Phase 7 — CLI / Terminal System
> สัปดาห์ 17–18 | ภาษา: C | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

สร้าง **Shell / Terminal System** แบบ Interactive สำหรับ VernisOS โดยรองรับ PS/2 Keyboard Input, Command Parsing, Session Management พร้อมระบบ Privilege Check ที่ผูกกับ SecurityContext จาก Phase 6 และส่ง Audit Event ไปยัง AI Bridge ที่จะพัฒนาใน Phase 8

---

## ภาพรวม

```
┌─────────────────────────────────────────────────────────────────┐
│                   VernisOS CLI Architecture                     │
├────────────────────┬────────────────────┬───────────────────────┤
│   Input Layer      │   Session Layer     │   Execution Layer     │
│                    │                    │                       │
│  PS/2 Keyboard     │  CliSession        │  Built-in Commands    │
│  IRQ1 Handler      │  ├─ username       │  ├─ root (lv 0)       │
│  Scancode→ASCII    │  ├─ privilege      │  ├─ admin (lv 50)     │
│  Backspace/Enter   │  ├─ uid            │  └─ user  (lv 100)    │
│                    │  └─ logged_in      │                       │
│  cli_readline()    │                    │  policy_check()       │
│                    │  login / logout    │  ai_send_event()      │
└────────────────────┴────────────────────┴───────────────────────┘
```

ระบบ CLI ทำงานบน Ring 0 แต่จำลอง Session ของผู้ใช้ด้วย CliSession ซึ่งบังคับ Privilege ก่อนทุก Command

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ประเภท | บทบาท |
|------|--------|--------|
| `include/cli.h` | Header | CliSession, ParsedCommand, Command table, API |
| `kernel/shell/cli.c` | C Source | readline, parser, dispatcher, privilege enforcement |
| `kernel/security/policy.c` | C Source | Policy check สำหรับ Command (เชื่อม Phase 12) |
| `kernel/drivers/ai_bridge.c` | C Source | ai_send_event() สำหรับส่ง DENY/AUDIT events |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. CliSession — ข้อมูล Session ผู้ใช้

```c
typedef struct {
    char     username[32];   // ชื่อผู้ใช้ที่ Login อยู่
    uint32_t privilege;      // 0=root, 50=admin, 100=user
    uint32_t uid;            // User ID
    bool     logged_in;      // สถานะ Login
} CliSession;
```

**ระดับ Privilege:**

```
0   → root   : เข้าถึงทุก Command รวมถึง shutdown, policy
50  → admin  : เข้าถึง Command จัดการระบบ เช่น ai, users, auditlog
100 → user   : เข้าถึง Command พื้นฐาน เช่น ls, cat, help
```

### 2. ParsedCommand — โครงสร้าง Command ที่ Parse แล้ว

```c
typedef struct {
    char cmd[32];        // ชื่อ Command หลัก
    char args[8][64];    // Arguments สูงสุด 8 ชิ้น
    int  argc;           // จำนวน Arguments
} ParsedCommand;
```

### 3. Built-in Commands ทั้ง 24 คำสั่ง

| Command | ระดับ Privilege | คำอธิบาย |
|---------|----------------|-----------|
| `shutdown` | root (0) | ปิดระบบ |
| `restart` | root (0) | รีสตาร์ทระบบ |
| `test` | root (0) | รัน Kernel Self-test |
| `policy` | root (0) | แสดง / แก้ไข Security Policy |
| `policy reload` | root (0) | โหลด Policy ใหม่จาก YAML |
| `ai` | admin (50) | ส่ง Query ไปยัง AI Engine |
| `users` | admin (50) | แสดงรายชื่อ Users |
| `auditlog` | admin (50) | แสดง Audit Log |
| `log` | admin (50) | ดู Kernel Log Buffer |
| `rm` | admin (50) | ลบไฟล์ |
| `mkdir` | admin (50) | สร้างไดเรกทอรี |
| `ls` | user (100) | แสดงรายการไฟล์ |
| `cat` | user (100) | แสดงเนื้อหาไฟล์ |
| `write` | user (100) | เขียนข้อความลงไฟล์ |
| `append` | user (100) | ต่อข้อความท้ายไฟล์ |
| `help` | user (100) | แสดงคำสั่งที่ใช้ได้ |
| `clear` | user (100) | ล้างหน้าจอ |
| `echo` | user (100) | แสดงข้อความ |
| `version` | user (100) | แสดงเวอร์ชัน VernisOS |
| `whoami` | user (100) | แสดง Username/Privilege ปัจจุบัน |
| `logout` | user (100) | ออกจาก Session |
| `login` | any | เข้าสู่ระบบ |

---

## โครงสร้างข้อมูล / API หลัก

### API Functions

```c
// เริ่มต้น CLI Session
void cli_init(void);

// อ่านบรรทัด Input จาก PS/2 Keyboard
void cli_readline(const char* prompt, char* buf, int max);

// Execute Command จาก Buffer
int cli_execute_command(const char* line);

// แสดง Prompt หลัง Login
void cli_show_prompt(void);
```

### Command Table Entry

```c
typedef struct {
    const char* name;            // ชื่อ Command
    uint32_t    min_privilege;   // ระดับขั้นต่ำที่ต้องการ
    int (*handler)(ParsedCommand* cmd);  // Function pointer
} BuiltinCommand;

// ตัวอย่าง entries
static BuiltinCommand builtins[] = {
    { "shutdown", 0,   cmd_shutdown  },
    { "ai",       50,  cmd_ai        },
    { "ls",       100, cmd_ls        },
    { "login",    255, cmd_login     },  // 255 = ไม่ต้อง Login ก่อน
    { NULL,       0,   NULL          }
};
```

---

## ขั้นตอนการทำงาน

### Login Flow

```
ระบบเริ่มต้น → cli_init()
      │
      ▼
แสดง "VernisOS login: " → cli_readline()
      │
      ▼
รับ username + password
      │
      ▼
ค้นหาใน user database
      │
   ┌──┴──┐
  ผ่าน   ล้มเหลว
   │       │
   ▼       ▼
กำหนด   แสดง "Login failed"
session  + ai_send_event(EVT, DENY)
privilege
   │
   ▼
แสดง "username@vernisOS:/> "
```

### Command Execution Security Flow

```
cli_execute_command(line)
         │
         ▼
    cli_parse(line) → ParsedCommand
         │
         ▼
    lookup builtin command table
         │
    ┌────┴────┐
  ไม่พบ     พบ
    │         │
    ▼         ▼
 "unknown   ตรวจ session.privilege > cmd.min_privilege?
 command"        │
            ┌────┴────┐
           YES        NO
            │          │
            ▼          ▼
    policy_check_    "Permission denied"
    command(cmd)     + audit_log()
            │        + ai_send_event(EVT, DENY)
       ┌────┴────┐
      PASS      BLOCK
       │           │
       ▼           ▼
   cmd.handler()  "Policy blocked"
                  + ai_send_event(EVT, DENY)
```

### readline() — PS/2 Input Handling

```c
void cli_readline(const char* prompt, char* buf, int max) {
    serial_print(prompt);   // แสดง Prompt ทาง Serial
    vga_print(prompt);      // แสดง Prompt ทาง VGA

    int pos = 0;
    while (pos < max - 1) {
        char c = keyboard_getchar();   // Block จนกว่า Key จะถูกกด

        if (c == '\n' || c == '\r') break;

        if (c == '\b' && pos > 0) {
            pos--;
            vga_backspace();           // ลบตัวอักษรบนหน้าจอ
            continue;
        }

        buf[pos++] = c;
        serial_putchar(c);             // Echo ไปยัง Serial
        vga_putchar(c);                // Echo ไปยัง VGA
    }
    buf[pos] = '\0';
}
```

### ai_poll_cmd() — IRQ0 Integration

**สำคัญ:** `ai_poll_cmd()` ถูกเรียกจาก **IRQ0 (PIT Timer 100Hz)** ทุก 10 ticks (ทุก 100ms) ไม่ใช่ใน readline loop เพราะถ้าเรียกใน readline loop จะได้รับ `0xFF` flood จาก COM2 ขณะ blocking

```
IRQ0 Handler (100Hz)
    │
    ├─ tick_counter++
    │
    ├─ tick % 10 == 0  → ai_poll_cmd()   ← ดึง CMD จาก AI Engine
    │
    └─ tick % 100 == 0 → ai_send_event(STAT, ...)  ← ส่งสถานะระบบ
```

---

## ผลลัพธ์

| รายการ | ผลลัพธ์ |
|--------|---------|
| Built-in Commands 24 คำสั่ง | ✅ ครบถ้วน พร้อม Privilege Levels |
| PS/2 Keyboard readline | ✅ รองรับ Backspace, Enter, Echo |
| Session Management | ✅ Login/Logout, Username/UID/Privilege |
| Privilege Enforcement | ✅ Block command ที่ไม่มีสิทธิ์ |
| Policy Check Integration | ✅ policy_check_command() ก่อน execute |
| Audit Logging | ✅ บันทึก DENY events |
| AI Event Integration | ✅ ai_send_event() ส่ง DENY/AUDIT |
| Prompt Format | ✅ "username@vernisOS:/>" |
| IRQ0 ai_poll_cmd | ✅ ทุก 10 ticks ไม่ block readline |

---

## สิ่งที่ต่อใน Phase ถัดไป

**Phase 8 — AI IPC Bridge** จะขยาย CLI ด้วย:
- Command `ai <query>` จะใช้ `ai_engine_query()` เพื่อส่งคำถามและรอรับคำตอบจาก Python AI Engine ผ่าน COM2
- `ai_send_event(DENY, ...)` ที่ CLI ส่งอยู่จะถูก Python AI Engine จัดการเป็น Security Alert
- `ai_poll_cmd()` จะรับ CMD frame จาก Python เพื่อสั่งงาน Kernel จากฝั่ง AI Engine
