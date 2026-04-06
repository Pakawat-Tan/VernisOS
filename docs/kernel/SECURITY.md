# VernisOS — ระบบความปลอดภัย (Security Subsystem)

> ไฟล์ที่เกี่ยวข้อง: `kernel/security/policy_enforce.c`, `sandbox.c`, `userdb.c`, `auditlog.c`, `sha256.c`

---

## ภาพรวม

ระบบ security ของ VernisOS มี 4 ชั้น:

```
User Input (CLI command)
        ↓
┌─────────────────────────────┐
│  1. Policy Enforcement      │  ← ตรวจ policy ก่อนรัน command
│     (policy_enforce.c)      │
├─────────────────────────────┤
│  2. Sandbox (Capability)    │  ← ตรวจ syscall permission ต่อ process
│     (sandbox.c)             │
├─────────────────────────────┤
│  3. User Authentication     │  ← login / password check
│     (userdb.c + sha256.c)  │
├─────────────────────────────┤
│  4. Audit Log               │  ← บันทึกทุก deny event
│     (auditlog.c + klog.c)  │
└─────────────────────────────┘
```

---

## 1. Policy Enforcement

**ไฟล์:** `kernel/security/policy_enforce.c`

### หน้าที่

ตรวจว่า user session มีสิทธิ์รัน command หรือไม่ โดยดูจาก policy rules ที่โหลดจาก disk (VPOL format)

### การทำงาน

```c
bool policy_check_command(const char *command, uint32_t privilege) {
    // 1. แยก command word แรก (เช่น "policy" จาก "policy reload")
    // 2. เรียก ai_kernel_engine_check_access(cmd_word)
    //    → คืนค่า minimum privilege ที่ต้องการ (0=root, 50=admin, 100=user, 255=allow all)
    // 3. map ค่าจาก policy space → CLI space
    // 4. คืน true ถ้า session privilege <= required (ค่าต่ำ = สิทธิ์สูงกว่า)
}
```

**ถ้าไม่มี rule สำหรับ command**: อนุญาตทั้งหมด (default allow)

### Policy File Format (VPOL Binary)

```
Header (16 bytes):
  magic[4]   = "VPOL"
  version[2] = 1
  count[2]   = จำนวน rules
  reserved[8]

Rules (ต่อจาก header, N rules):
  command[16]   = ชื่อ command
  min_priv[1]   = minimum privilege (0/50/100)
  flags[1]      = 0
  reserved[2]
```

### สร้าง Policy จาก YAML

```bash
# ai/config/policy.yaml
commands:
  shutdown: root
  restart: root
  test: root
  policy: root
  ai: admin
  users: admin
  auditlog: admin
  log: admin
  rm: admin
  ls: user
  cat: user
  help: user

# Compile → VPOL binary
python3 ai/tools/policy_compile.py ai/config/policy.yaml -o make/policy.bin
```

---

## 2. Sandbox (Capability-Based)

**ไฟล์:** `kernel/security/sandbox.c`

### โครงสร้าง SecurityContext

```c
typedef struct {
    uint32_t    pid;
    ProcessType type;           // Kernel / System / User
    uint64_t    capabilities;   // bitmask
    uint8_t     runtime_ring;   // 0 / 1 / 2 / 3
    struct {
        uint32_t max_file_size;
        uint32_t max_open_files;
        uint32_t max_memory;
    } limits;
} SecurityContext;
```

Static pool ของ 16 SecurityContext

### Syscall Filtering

```c
bool sandbox_check_syscall(uint32_t pid, uint32_t syscall_num) {
    // 1. หา SecurityContext ของ pid
    // 2. Kernel process → allow all
    // 3. ตรวจ capability bit สำหรับ syscall กลุ่มพิเศษ:
    //    IPC (20-22, 26-27)      → CAP_IPC_SEND / CAP_IPC_RECV
    //    Module load/unload (28-29) → CAP_MODULE_LOAD / CAP_MODULE_UNLOAD
    //    Module execute (31)      → CAP_MODULE_LOAD
    // 4. ตรวจ whitelist bitmask ตาม process type
    // 5. ถ้า deny → ai_send_event(DENY, ...) + คืน false
}
```

### Syscall Whitelist ต่อ ProcessType

| ProcessType | Syscall ที่อนุญาต |
|------------|-----------------|
| Kernel | 0–32 ทั้งหมด (full access) |
| System | 0, 1, 2, 20–27 |
| User | 0, 1, 2, 20–22 |

### User Memory Validation

```c
bool sandbox_validate_user_pointer(uint32_t pid, uintptr_t ptr, size_t len) {
    // pid ของ User process → user_memory_base = 0x1000000 (16 MB)
    //                         user_memory_size = 4 MB
    // ตรวจว่า ptr..ptr+len อยู่ใน [base, base+size]
}
```

---

## 3. User Authentication

**ไฟล์:** `kernel/security/userdb.c`, `kernel/security/sha256.c`

### UserRecord

```c
typedef struct {
    char     username[32];
    uint8_t  password_hash[32];   // SHA-256 ของ password
    uint32_t privilege;            // 0=root, 50=admin, 100=user
    uint32_t uid;
    uint8_t  flags;               // USER_FLAG_ACTIVE | USER_FLAG_LOCKED | USER_FLAG_NO_PASSWORD
    uint8_t  reserved[19];
} UserRecord;                     // 96 bytes / record
```

### การโหลด User Database

```c
void userdb_init(void) {
    // อ่านไฟล์ /etc/shadow จาก VernisFS
    // parse UserRecord structs ออกมาเก็บใน static array
}
```

### Authentication

```c
int userdb_authenticate(const char *username, const char *password) {
    // 1. หา user ใน database
    // 2. ตรวจ flags: USER_FLAG_ACTIVE ต้อง set, USER_FLAG_LOCKED ต้องไม่ set
    // 3. ถ้า USER_FLAG_NO_PASSWORD → ผ่านเลย
    // 4. SHA-256(password) แล้วเปรียบเทียบกับ password_hash
    // 5. คืน privilege level ถ้า OK, -1 ถ้าล้มเหลว
}
```

### SHA-256 Implementation

`sha256.c` implement SHA-256 แบบ pure C สำหรับใช้ใน kernel (ไม่พึ่ง libc):

```c
void sha256_compute(const uint8_t *data, size_t len, uint8_t *hash_out);
bool sha256_compare(const uint8_t *hash1, const uint8_t *hash2);  // constant-time compare
```

---

## 4. Audit Log

**ไฟล์:** `kernel/security/auditlog.c`, `include/auditlog.h`

### หน้าที่

บันทึกทุกครั้งที่ command ถูกปฏิเสธ (policy deny, sandbox deny)

```c
void auditlog_init(void);
void auditlog_record_deny(const char *username, const char *command,
                           const char *reason, uint32_t tick);
void auditlog_print_all(void);   // ใช้โดย `auditlog` command
```

### Kernel Log (klog)

**ไฟล์:** `kernel/log/klog.c`

Structured kernel log พร้อม level:

```c
typedef enum {
    KLOG_DEBUG   = 0,
    KLOG_INFO    = 1,
    KLOG_WARNING = 2,
    KLOG_ERROR   = 3,
    KLOG_CRITICAL = 4,
} KlogLevel;

void klog_init(void);
void klog_write(KlogLevel level, const char *tag, const char *message);
```

ดู log ผ่าน CLI:
```
VernisOS> log           # แสดง 20 entries ล่าสุด
VernisOS> log 50        # แสดง 50 entries
VernisOS> log error     # แสดงเฉพาะ ERROR+
```

---

## ขั้นตอน Security ตอน Login

```
VernisOS> login
Username: alice
Password: ****
     ↓
userdb_authenticate("alice", "****")
  → SHA-256("****") vs stored hash
  → return privilege = 100 (user)
     ↓
session->username = "alice"
session->privilege = 100
session->uid = alice's uid
     ↓
alice@vernisOS:/>
```

---

## ขั้นตอน Security ตอน Execute Command

```
alice@vernisOS:/> shutdown
     ↓
cli_execute_command()
  1. หา command "shutdown" ใน g_builtins[]
     min_privilege = CLI_PRIV_ROOT (0)

  2. ตรวจ session privilege:
     session->privilege (100) > min_privilege (0)
     → DENIED

  3. ก่อน return:
     - auditlog_record_deny("alice", "shutdown", "privilege", tick)
     - klog_write(KLOG_WARNING, "CLI", "alice denied: shutdown")
     - ai_send_event("DENY", "alice|shutdown|privilege")

  4. พิมพ์:
     Permission denied: requires root privilege
```
