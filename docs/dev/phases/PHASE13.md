# Phase 13 — Kernel AI Policy Enforcement + Auth Layer
> สัปดาห์ 33–35 | ภาษา: C | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

พัฒนา **Authentication Layer** และ **Audit System** แบบ bare-metal ใน C โดยไม่พึ่ง libc โดยรวม UserDB (ข้อมูลผู้ใช้), SHA-256 (password hashing), Policy Enforcement (Phase 12), และ Audit Log เข้าเป็น security stack ที่สมบูรณ์ ระบบนี้ทำให้ VernisOS มี login, privilege separation, และ command access control แบบครบวงจร

---

## ภาพรวม

```
┌──────────────────────────────────────────────────────────┐
│                  Security Stack Overview                  │
│                                                          │
│  User input: "login root"                                │
│       │                                                  │
│       ▼                                                  │
│  ┌──────────────────────┐                               │
│  │   userdb.c           │  ← อ่าน /etc/shadow จาก VernisFS│
│  │   userdb_authenticate│                               │
│  │   (username+password)│                               │
│  └──────────┬───────────┘                               │
│             │  SHA-256 hash + compare                   │
│  ┌──────────▼───────────┐                               │
│  │   sha256.c           │  ← pure C, no libc            │
│  │   sha256_compute()   │                               │
│  │   sha256_compare()   │  ← constant-time compare      │
│  └──────────┬───────────┘                               │
│             │  privilege (0/50/100) หรือ -1             │
│             ▼                                           │
│  session.privilege = 100  (root logged in)              │
│  session.uid = 0                                        │
│  session.username = "root"                              │
│                                                          │
│  User input: "shutdown"                                  │
│       │                                                  │
│       ▼                                                  │
│  ┌──────────────────────┐                               │
│  │  policy_enforce.c    │  ← Phase 12 VPOL rules        │
│  │  policy_check_command│                               │
│  │  (cmd, privilege)    │                               │
│  └──────────┬───────────┘                               │
│        ┌────┴────┐                                       │
│      ALLOW     DENY                                      │
│        │         │                                       │
│        ▼         ▼                                       │
│    execute   ┌──────────────┐                           │
│              │ auditlog.c   │  ← ring buffer log        │
│              │ record_deny()│                           │
│              └──────────────┘                           │
└──────────────────────────────────────────────────────────┘
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | หน้าที่ |
|------|---------|
| `kernel/security/policy_enforce.c` | Runtime policy enforcement (Phase 12 + 13) |
| `kernel/security/userdb.c` | User database: load, authenticate, manage |
| `kernel/security/sha256.c` | Pure C SHA-256: compute + constant-time compare |
| `kernel/security/auditlog.c` | Ring buffer audit log: record + display |
| `include/userdb.h` | UserRecord struct + API declarations |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. UserRecord Struct — 96 Bytes Fixed Size

```c
// include/userdb.h

#define USERNAME_MAX     32
#define HASH_SIZE        32    // SHA-256 = 256 bits = 32 bytes
#define USERDB_MAX_USERS 16

#define USER_FLAG_ACTIVE      0x01  // account ใช้งานได้
#define USER_FLAG_LOCKED      0x02  // account ถูกล็อก
#define USER_FLAG_NO_PASSWORD 0x04  // ไม่ต้องใส่ password

typedef struct {
    char    username[32];       // null-terminated
    uint8_t password_hash[32];  // SHA-256 hash
    uint8_t privilege;          // 0=user, 50=admin, 100=root
    uint8_t uid;                // unique user ID
    uint8_t flags;              // USER_FLAG_*
    uint8_t reserved[19];       // padding → รวม 96 bytes
} __attribute__((packed)) UserRecord;

// รวม struct size: 32+32+1+1+1+19 = 86... แต่ align แล้ว = 96 bytes
```

```
UserRecord layout (96 bytes):
┌──────────────────────────────────┐
│  username[32]   (null-padded)    │  0–31
├──────────────────────────────────┤
│  password_hash[32]  (SHA-256)    │  32–63
├──┬──┬──┬──────────────────────── ┤
│p │u │f │  reserved[19]          │  64–95
│r │i │l │  (future: expiry, etc) │
│i │d │a │                        │
│v │  │g │                        │
└──┴──┴──┴────────────────────────┘
```

### 2. userdb.c — User Database

**userdb_init()** — โหลด user records จาก VernisFS:

```c
#define SHADOW_PATH "/etc/shadow"

static UserRecord user_table[USERDB_MAX_USERS];
static int        user_count = 0;

int userdb_init(void) {
    VernisFile *f = vernisfs_open(SHADOW_PATH);
    if (!f) {
        klog("[USERDB] /etc/shadow not found, using defaults");
        _userdb_load_defaults();
        return -1;
    }

    user_count = vernisfs_read(f, user_table,
                               sizeof(user_table)) / sizeof(UserRecord);
    vernisfs_close(f);

    klog("[USERDB] Loaded %d users from /etc/shadow", user_count);
    return user_count;
}
```

**userdb_authenticate()** — ตรวจสอบ username + password:

```c
int userdb_authenticate(const char *username, const char *password) {
    // หา user
    UserRecord *user = NULL;
    for (int i = 0; i < user_count; i++) {
        if (strncmp(user_table[i].username, username, USERNAME_MAX) == 0) {
            user = &user_table[i];
            break;
        }
    }
    if (!user) return -1;  // user ไม่พบ

    // ตรวจสอบ flags
    if (!(user->flags & USER_FLAG_ACTIVE)) return -1;   // ไม่ active
    if (user->flags & USER_FLAG_LOCKED)   return -1;   // ถูกล็อก

    // NO_PASSWORD flag
    if (user->flags & USER_FLAG_NO_PASSWORD) {
        return user->privilege;
    }

    // คำนวณ SHA-256 ของ password ที่ใส่มา
    uint8_t input_hash[32];
    sha256_compute((const uint8_t *)password, strlen(password), input_hash);

    // เปรียบเทียบแบบ constant-time
    if (!sha256_compare(input_hash, user->password_hash)) {
        return -1;  // password ผิด
    }

    return user->privilege;  // success → return privilege
}
```

### 3. sha256.c — Pure C SHA-256

```c
// kernel/security/sha256.c
// SHA-256 implementation — ไม่ใช้ libc ใดๆ

#define SHA256_ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define SHA256_CH(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define SHA256_MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, /* ... */
};

void sha256_compute(const uint8_t *data, size_t len, uint8_t *hash_out) {
    // Standard SHA-256 implementation
    // ใช้ได้กับ password สั้นๆ บน bare-metal kernel
    // ...
}

int sha256_compare(const uint8_t *hash1, const uint8_t *hash2) {
    // Constant-time comparison — ป้องกัน timing attack
    // ไม่ใช้ memcmp ทั่วไป เพราะ short-circuit บนข้อมูลที่ต่างกัน
    uint8_t result = 0;
    for (int i = 0; i < 32; i++) {
        result |= hash1[i] ^ hash2[i];
    }
    return result == 0;  // 1 = equal, 0 = not equal
}
```

**ทำไม Constant-time Compare:**

```
memcmp ทั่วไป:
  "abcd" vs "xbcd" → ออกหลังตำแหน่ง 0 (เร็ว)
  "abcd" vs "abcx" → ออกหลังตำแหน่ง 3 (ช้ากว่า)
  → ผู้โจมตีวัด timing ได้ → เดา hash ทีละ byte

sha256_compare (constant-time):
  เปรียบเทียบทุก byte เสมอ 32 รอบ
  → timing เท่ากันทุกกรณี
  → ป้องกัน timing attack
```

### 4. auditlog.c — Audit Log System

```c
// kernel/security/auditlog.c

#define AUDITLOG_MAX 256

typedef struct {
    char     username[32];
    char     command[64];
    char     reason[64];
    uint32_t tick;          // kernel tick timestamp
    uint8_t  type;          // AUDIT_DENY, AUDIT_LOGIN, AUDIT_LOGOUT
} AuditEntry;

static AuditEntry audit_ring[AUDITLOG_MAX];
static int audit_head = 0;   // next write position
static int audit_count = 0;  // total entries written

void auditlog_init(void) {
    audit_head  = 0;
    audit_count = 0;
}

void auditlog_record_deny(const char *username, const char *cmd,
                           const char *reason, uint32_t tick) {
    AuditEntry *e = &audit_ring[audit_head % AUDITLOG_MAX];
    strncpy(e->username, username, 31);
    strncpy(e->command,  cmd,      63);
    strncpy(e->reason,   reason,   63);
    e->tick = tick;
    e->type = AUDIT_DENY;

    audit_head  = (audit_head + 1) % AUDITLOG_MAX;
    audit_count++;
}

void auditlog_print_all(void) {
    // ใช้ใน CLI command 'auditlog'
    int start = (audit_count >= AUDITLOG_MAX)
                    ? audit_head : 0;
    int n = (audit_count >= AUDITLOG_MAX)
                    ? AUDITLOG_MAX : audit_count;
    for (int i = 0; i < n; i++) {
        AuditEntry *e = &audit_ring[(start + i) % AUDITLOG_MAX];
        kprintf("[%u] DENY user=%s cmd=%s reason=%s\n",
                e->tick, e->username, e->command, e->reason);
    }
}
```

---

## โครงสร้างข้อมูล / API หลัก

### API Summary

| Function | Signature | คืนค่า |
|----------|-----------|--------|
| `userdb_init` | `int userdb_init(void)` | จำนวน users โหลดได้ |
| `userdb_authenticate` | `int userdb_authenticate(user, pass)` | privilege หรือ -1 |
| `sha256_compute` | `void sha256_compute(data, len, out)` | — |
| `sha256_compare` | `int sha256_compare(h1, h2)` | 1=match, 0=mismatch |
| `policy_check_command` | `int policy_check_command(cmd, priv)` | ALLOW/DENY |
| `ai_kernel_engine_check_access` | `int ai_kernel_engine_check_access(cmd)` | min_priv |
| `auditlog_init` | `void auditlog_init(void)` | — |
| `auditlog_record_deny` | `void auditlog_record_deny(user,cmd,reason,tick)` | — |
| `auditlog_print_all` | `void auditlog_print_all(void)` | — |

### Session Struct

```c
typedef struct {
    char    username[32];
    uint8_t privilege;   // 0=user, 50=admin, 100=root
    uint8_t uid;
    uint8_t logged_in;   // 1 = มี session อยู่
} KernelSession;

extern KernelSession current_session;
```

---

## ขั้นตอนการทำงาน

### Login Flow

```
1. ผู้ใช้พิมพ์: "login root"
         │
         ▼
2. cli_execute("login root")
         │
         ▼
3. อ่าน password: "Password: " (no echo)
         │
         ▼
4. userdb_authenticate("root", password_input)
    ├── หา UserRecord ชื่อ "root"
    ├── check ACTIVE flag ✓
    ├── check LOCKED flag ✓ (ไม่ถูกล็อก)
    ├── sha256_compute(password_input) → input_hash
    └── sha256_compare(input_hash, stored_hash)
         │
    ┌────┴────┐
  match     no match
    │           │
    ▼           ▼
5. Set session  "Login failed"
   .privilege=100
   .uid=0
   .username="root"
   .logged_in=1
    │
    ▼
6. แสดง prompt: "vernis[root]> "
```

### Execute Flow (with Policy Check)

```
1. ผู้ใช้พิมพ์: "shutdown"
         │
         ▼
2. cli_execute("shutdown", session.privilege=50)
         │
         ▼
3. check builtin privilege (hard-coded minimums)
         │
         ▼
4. policy_check_command("shutdown", 50)
    ├── ai_kernel_engine_check_access("shutdown") → 100
    └── 50 < 100 → DENY
         │
         ▼
5. DENY branch:
    ├── แสดง "Permission denied: requires root"
    ├── auditlog_record_deny("user1", "shutdown",
    │       "insufficient privilege", kernel_tick)
    ├── klog("[POLICY] deny: shutdown by user1 (priv=50)")
    └── ai_send_event("DENY", current_pid, "shutdown")
         │
         ▼
6. BehaviorMonitor รับ DENY event
   → ตรวจ DENY pattern rules
   → หาก DENY × 3 ใน 5s → alert HIGH
```

---

## ผลลัพธ์

### ตัวอย่าง Login + Enforcement

```
VernisOS v0.13  [kernel secure mode]

vernis> login root
Password: ****
Welcome, root. Privilege: 100 (root)

vernis[root]> shutdown
Shutting down VernisOS... OK

vernis> login user1
Password: ****
Welcome, user1. Privilege: 0 (user)

vernis[user1]> shutdown
Permission denied: 'shutdown' requires privilege 100 (root)
This action has been logged.

vernis[user1]> auditlog
Permission denied: 'auditlog' requires privilege 50 (admin)
This action has been logged.

vernis[user1]> ls
/boot  /etc  /home  /tmp
```

### ตัวอย่าง Audit Log

```
vernis[root]> auditlog
Audit log (last 10 entries):
[tick=12450] DENY user=user1 cmd=shutdown reason=insufficient privilege
[tick=12461] DENY user=user1 cmd=auditlog reason=insufficient privilege
[tick=12890] DENY user=guest cmd=rm     reason=insufficient privilege
[tick=13100] DENY user=guest cmd=ai     reason=insufficient privilege
```

### Security Properties

| Property | Implementation | คำอธิบาย |
|----------|---------------|---------|
| Password hashing | SHA-256 pure C | ไม่เก็บ plaintext |
| Timing attack resistant | constant-time compare | ป้องกัน side-channel |
| Account locking | USER_FLAG_LOCKED | ล็อก account ที่ถูก compromise |
| Audit trail | ring buffer 256 entries | บันทึก deny events ทั้งหมด |
| Privilege separation | 3 levels (0/50/100) | user / admin / root |
| No libc dependency | pure C implementation | ใช้ได้บน bare-metal |

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 14 และ beyond จะขยาย security stack:

- **Capability System** — per-process capabilities แทน privilege levels
- **Mandatory Access Control** — เพิ่ม label-based access control
- **Secure IPC** — ตรวจสอบ privilege ก่อน IPC ทุกครั้ง
- **Key Storage** — เก็บ cryptographic keys ใน kernel-only memory region

```
Phase 13 (done):
    UserDB + SHA-256 + Policy + AuditLog
         │
         ▼
Phase 14 (planned):
    Capability bits per process
    → ยืดหยุ่นกว่า 3-level privilege
    → ลด attack surface ต่อ process
```

**Integration ที่สมบูรณ์แล้ว (Phase 10–13):**

```
Phase 10: BehaviorMonitor  — ตรวจจับ anomaly
Phase 11: AutoTuner        — ปรับ kernel parameters
Phase 12: PolicySystem     — กำหนด access rules (YAML→VPOL)
Phase 13: AuthLayer        — ตรวจสอบ user identity + privilege
                              ┌──────────────────────┐
                              │  VernisOS Security   │
                              │  Stack Complete ✅   │
                              └──────────────────────┘
```
