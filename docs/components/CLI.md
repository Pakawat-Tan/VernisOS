# VernisOS — CLI Shell (เชลล์บรรทัดคำสั่ง)

> ไฟล์ที่เกี่ยวข้อง: `kernel/shell/cli.c`, `include/cli.h`

---

## ภาพรวม

CLI Shell ของ VernisOS เป็น interactive shell ที่วิ่งโดยตรงใน kernel (Ring 0)
ไม่มี user space binary — ทุก command เป็น built-in

```
kernel_main()
    └─ cli_shell_loop()          ← วน loop ตลอดเวลาที่ kernel รัน
           └─ cli_readline()     ← รับ input จาก keyboard
           └─ cli_process_line() ← parse และ execute command
```

---

## คำสั่งทั้งหมด (24 commands)

### ทั่วไป (Privilege: User)

| Command | Syntax | คำอธิบาย |
|---------|--------|----------|
| `help` | `help` | แสดงรายการคำสั่งทั้งหมด |
| `clear` | `clear` | ล้างหน้าจอ |
| `echo` | `echo <text>` | พิมพ์ข้อความ |
| `info` | `info` | แสดง system info (uptime, heap, process count, AI status, arch) |
| `whoami` | `whoami` | แสดง username และ privilege level |
| `exit` | `exit` | ออกจาก shell |

### Session Management

| Command | Privilege | คำอธิบาย |
|---------|-----------|----------|
| `login` | user | สลับ user (ถาม password) |
| `su` | user | รันคำสั่งในฐานะ root (ถาม root password) |
| `logout` | user | กลับไปเป็น root session |

### Process Management

| Command | Privilege | คำอธิบาย |
|---------|-----------|----------|
| `ps` | user | แสดงรายการ process ทั้งหมด |

### System Administration

| Command | Privilege | คำอธิบาย |
|---------|-----------|----------|
| `shutdown` | root | ปิด VernisOS |
| `restart` | root | reset ระบบ |
| `test` | root | รัน kernel self-tests |
| `users` | admin | แสดงรายชื่อ user ทั้งหมด |
| `policy` | root | แสดงหรือโหลด policy config |
| `ai` | admin | query AI engine / แสดง AI status |
| `auditlog` | admin | แสดง security audit log |
| `log` | admin | ดู kernel log |

### Filesystem (VernisFS)

| Command | Privilege | Syntax | คำอธิบาย |
|---------|-----------|--------|----------|
| `ls` | user | `ls [path]` | แสดงรายการไฟล์ |
| `cat` | user | `cat <path>` | แสดงเนื้อหาไฟล์ |
| `write` | user | `write <path> <content...>` | สร้าง/เขียนทับไฟล์ |
| `append` | user | `append <path> <content...>` | เพิ่มข้อมูลต่อท้ายไฟล์ |
| `rm` | admin | `rm <path>` | ลบไฟล์ |
| `mkdir` | user | `mkdir <path>` | สร้างไดเรกทอรี |

---

## Privilege Levels

| Level | ค่า | สิทธิ์ |
|-------|-----|-------|
| `CLI_PRIV_ROOT` | 0 | ทุกอย่าง (shutdown, restart, test, policy) |
| `CLI_PRIV_ADMIN` | 50 | Admin commands (ai, users, auditlog, log, rm) |
| `CLI_PRIV_USER` | 100 | Basic commands |

ถ้า session privilege > minimum ที่ command ต้องการ → denied และบันทึก audit log

---

## cli_readline() — Line Editor

readline ของ VernisOS รองรับ:

```
← →           เลื่อน cursor ซ้าย/ขวา
Home / End     ไปต้น/ท้ายบรรทัด
Backspace      ลบ char ก่อน cursor
Delete         ลบ char หลัง cursor
↑ ↓            History navigation (ring buffer 16 entries)
Ctrl+C         ล้างบรรทัดปัจจุบัน
Enter          ส่ง command
```

### VGA Cursor Sync

readline ใช้ VGA hardware cursor แสดงตำแหน่ง:
```c
vga_set_cursor(cursor_col, cursor_row);  // อัปเดต VGA CRTC register
```

### Idle Work ระหว่างรอ Input

ระหว่าง polling keyboard (`hlt` loop):
```c
while (!(c = keyboard_read_char())) {
    __asm__("hlt");
    kernel_idle_work();  // ← เรียก AI engine tick ระหว่างรอ
}
```

---

## Command Execution Flow

```
cli_process_line(line)
    │
    ├─ cli_parse_command()
    │   ├─ tokenize ด้วย space (รองรับ quoted strings)
    │   ├─ ตรวจ `&` ท้าย → background flag
    │   └─ ตรวจ `>` → redirect
    │
    └─ cli_execute_command(session, parsed_cmd)
        │
        ├─ หา command ใน g_builtins[] table
        │
        ├─ ตรวจ privilege:
        │   session->privilege <= command->min_privilege?
        │
        ├─ ตรวจ policy:
        │   policy_check_command(cmd_name, session->privilege)
        │   ถ้า denied → บันทึก audit log + klog + AI event
        │
        └─ เรียก command->handler(session, parsed_cmd)
```

---

## Data Structures

### CliSession

```c
typedef struct {
    char     username[32];       // ชื่อ user ปัจจุบัน
    uint32_t uid;                // User ID
    uint32_t privilege;          // 0=root, 50=admin, 100=user
    bool     is_active;          // ถ้า false → shell_loop ออก
    uint32_t commands_executed;  // นับ command ที่รัน
} CliSession;
```

### ParsedCommand

```c
typedef struct {
    char    *argv[16];   // arguments (argv[0] = command name)
    int      argc;       // จำนวน arguments
    bool     background; // มี & ท้ายไหม?
    char    *redirect;   // output redirect path
    char     raw[256];   // raw input line
} ParsedCommand;
```

### BuiltinCommand

```c
typedef struct {
    const char *name;
    const char *description;
    int (*handler)(CliSession *, const ParsedCommand *);
    uint32_t    min_privilege;   // CLI_PRIV_ROOT / CLI_PRIV_ADMIN / CLI_PRIV_USER
} CliBuiltinCommand;
```

---

## ps Command Output

```
VernisOS> ps
PID  PPID  STATE    PRI  TYPE    RING  CPU(ms)  MEM(KB)  COMMAND
---  ----  -------  ---  ------  ----  -------  -------  -------
1    0     Standby  100  Kernel  R0    0        0        init
2    1     Standby  90   System  R0    0        0        ai_engine
```

สถานะ Process:
- `New` / `Standby` / `Running` / `Waiting` / `Suspended` / `Terminated` / `Zombie`

---

## ตัวอย่าง Session

```
                  ██╗   ██╗███████╗██████╗ ███╗   ██╗██╗███████╗ ██████╗ ███████╗
                  ██║   ██║██╔════╝██╔══██╗████╗  ██║██║██╔════╝██╔═══██╗██╔════╝
                  ██║   ██║█████╗  ██████╔╝██╔██╗ ██║██║███████╗██║   ██║███████╗
                  ╚██╗ ██╔╝██╔══╝  ██╔══██╗██║╚██╗██║██║╚════██║██║   ██║╚════██║
                   ╚████╔╝ ███████╗██║  ██║██║ ╚████║██║███████║╚██████╔╝███████║
                    ╚═══╝  ╚══════╝╚═╝  ╚═╝╚═╝  ╚═══╝╚═╝╚══════╝ ╚═════╝ ╚══════╝
  VernisOS v0.1.0-dev | Microkernel OS with AI | Type 'help' for commands
  [self-test] PASS (7/7)   [vfs] VernisFS mounted   [ai] ONLINE

root@vernisOS:/> help
  Available commands:
  ...

root@vernisOS:/> login
Username: alice
Password: ****
Logged in as alice (privilege=user)

alice@vernisOS:/> shutdown
Permission denied: requires root privilege

alice@vernisOS:/> mkdir /home
alice@vernisOS:/> write /home/note.txt Hello from Alice
alice@vernisOS:/> cat /home/note.txt
Hello from Alice
alice@vernisOS:/> ls /home
  [file] note.txt             16 B
```
