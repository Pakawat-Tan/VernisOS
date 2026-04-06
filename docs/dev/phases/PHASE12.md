# Phase 12 — Policy System + YAML Config Loader
> สัปดาห์ 30–32 | ภาษา: Python (ruamel.yaml) + C | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

พัฒนาระบบ **Policy System** แบบ end-to-end ตั้งแต่ YAML definition → binary compile → kernel load → runtime enforcement โดยใช้ binary format `VPOL` ที่มีขนาดคงที่และโหลดได้เร็วจาก ATA PIO sector ระบบนี้ทำให้ผู้ดูแลระบบสามารถกำหนด access control policy ผ่าน YAML โดยไม่ต้องแก้โค้ด C หรือ rebuild kernel

---

## ภาพรวม

```
 ┌─────────────────────────────────────────────────────────┐
 │           Policy Lifecycle                              │
 │                                                         │
 │  policy.yaml                                            │
 │     │  (human-readable config)                         │
 │     ▼                                                   │
 │  policy_compile.py                                      │
 │     │  struct.pack() → binary VPOL                     │
 │     ▼                                                   │
 │  make/policy.bin   ← เขียนลง disk sector 4096          │
 │     │                                                   │
 │     ▼  (kernel boot / reload)                          │
 │  policy_loader.c                                        │
 │     │  ATA PIO read sector 4096                        │
 │     │  parse VPOL header + rules[]                     │
 │     ▼                                                   │
 │  policy_enforce.c                                       │
 │     │  policy_check_command(cmd, privilege)            │
 │     │  → allow / deny                                  │
 │     ▼                                                   │
 │  CLI + AI Engine                                        │
 │     │  ทุก command ถูกตรวจ policy ก่อน execute        │
 └─────────────────────────────────────────────────────────┘
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | หน้าที่ |
|------|------|---------|
| `ai/tools/policy_compile.py` | Python | Compile YAML → VPOL binary |
| `ai/policy_manager.py` | Python | Manage policy: reload, check, send to kernel |
| `ai/config/policy.yaml` | YAML | Human-readable policy definitions |
| `kernel/security/policy_loader.c` | C | Load VPOL binary จาก ATA PIO disk |
| `kernel/security/policy_enforce.c` | C | Enforce policy ณ runtime |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. VPOL Binary Format

**Header (16 bytes):**

```
Offset  Size  Field        คำอธิบาย
──────  ────  ──────────   ──────────────────────────────
0       4     magic        "VPOL" (0x56 0x50 0x4F 0x4C)
4       1     version      version = 1
5       1     count        จำนวน rules (max 255)
6       10    reserved     ศูนย์ทั้งหมด (future use)
```

**Each Rule (20 bytes):**

```
Offset  Size  Field        คำอธิบาย
──────  ────  ──────────   ──────────────────────────────
0       16    command      ชื่อ command (null-padded)
16      1     min_priv     privilege ขั้นต่ำ (0/50/100)
17      1     flags        0x01=ALLOW, 0x02=DENY, 0x04=AUDIT
18      2     reserved     ศูนย์ (future use)
```

```
Binary layout (2 rules = 56 bytes total):
┌────────────────────────────────────────────────┐
│  VPOL (4) │ ver(1) │ count(1) │ reserved(10)   │  ← Header 16B
├─────────────────────────────────────┬──┬──┬────┤
│  "shutdown\0\0\0\0\0\0\0"  (16)   │100│01│00 00│  ← Rule 1: 20B
├─────────────────────────────────────┬──┬──┬────┤
│  "ls\0\0\0\0\0\0\0\0\0\0\0\0\0"  (16)│ 0│01│00 00│  ← Rule 2: 20B
└─────────────────────────────────────────────────┘
```

### 2. policy.yaml — Human-Readable Config

```yaml
# ai/config/policy.yaml
version: 1
description: "VernisOS default access policy"

commands:
  # root only (privilege = 100)
  shutdown:    root
  restart:     root
  test:        root
  policy:      root

  # admin required (privilege = 50)
  ai:          admin
  users:       admin
  auditlog:    admin
  log:         admin
  rm:          admin

  # any user (privilege = 0)
  ls:          user
  cat:         user
  help:        user
```

**Privilege mapping:**

| YAML value | min_priv byte | คำอธิบาย |
|-----------|---------------|---------|
| `root` | 100 | เฉพาะ root เท่านั้น |
| `admin` | 50 | admin ขึ้นไป |
| `user` | 0 | ทุกคน |
| *(ไม่มี rule)* | 255 | allow all (default) |

### 3. policy_compile.py — YAML → Binary

```python
# ai/tools/policy_compile.py

import struct
from ruamel.yaml import YAML

PRIV_MAP = {"root": 100, "admin": 50, "user": 0}
HEADER_FMT = "4sBB10s"   # magic, version, count, reserved
RULE_FMT   = "16sBB2s"   # command, min_priv, flags, reserved

def compile_policy(yaml_path: str, output_path: str):
    yaml = YAML()
    with open(yaml_path) as f:
        data = yaml.load(f)

    rules = []
    for cmd, level in data["commands"].items():
        min_priv = PRIV_MAP[level]
        cmd_bytes = cmd.encode()[:16].ljust(16, b'\x00')
        rule = struct.pack(RULE_FMT, cmd_bytes, min_priv, 0x01, b'\x00\x00')
        rules.append(rule)

    header = struct.pack(HEADER_FMT,
        b"VPOL", 1, len(rules), b'\x00' * 10)

    with open(output_path, "wb") as f:
        f.write(header)
        for rule in rules:
            f.write(rule)

    print(f"Compiled {len(rules)} rules → {output_path}")
    print(f"Total size: {16 + len(rules)*20} bytes")
```

**ใช้งาน:**

```bash
python3 ai/tools/policy_compile.py ai/config/policy.yaml -o make/policy.bin
# Output: Compiled 14 rules → make/policy.bin
#         Total size: 296 bytes
```

### 4. PolicyManager (Python) — Runtime Management

```python
class PolicyManager:
    def reload_policy(self, yaml_path: str):
        """Recompile YAML และส่ง policy ใหม่ไป kernel"""
        compile_policy(yaml_path, "/tmp/policy_new.bin")
        self._send_policy_to_kernel("/tmp/policy_new.bin")

    def check_access(self, command: str, privilege: int) -> bool:
        """ตรวจสอบ access ฝั่ง Python"""
        rule = self._rules.get(command)
        if rule is None:
            return True  # ไม่มี rule = allow
        return privilege >= rule.min_priv

    def _send_policy_to_kernel(self, bin_path: str):
        """ส่ง policy binary ผ่าน CMD|POLICY frame"""
        data = open(bin_path, "rb").read().hex()
        self.send(f"CMD|0|POLICY|load|{data}")
```

### 5. policy_loader.c — Kernel Side Load

```c
// kernel/security/policy_loader.c

#define POLICY_DISK_SECTOR 4096
#define VPOL_MAGIC         0x4C4F5056  // "VPOL" little-endian

typedef struct {
    char     magic[4];
    uint8_t  version;
    uint8_t  count;
    uint8_t  reserved[10];
} __attribute__((packed)) VpolHeader;   // 16 bytes

typedef struct {
    char    command[16];
    uint8_t min_priv;
    uint8_t flags;
    uint8_t reserved[2];
} __attribute__((packed)) VpolRule;     // 20 bytes

static VpolRule policy_rules[64];
static int      policy_rule_count = 0;

int policy_load_from_disk(void) {
    uint8_t buf[512];
    ata_pio_read_sector(POLICY_DISK_SECTOR, buf);

    VpolHeader *hdr = (VpolHeader *)buf;
    if (memcmp(hdr->magic, "VPOL", 4) != 0) {
        klog("[POLICY] Invalid magic, using defaults");
        return -1;
    }

    policy_rule_count = hdr->count;
    VpolRule *rules = (VpolRule *)(buf + sizeof(VpolHeader));
    memcpy(policy_rules, rules,
           policy_rule_count * sizeof(VpolRule));

    klog("[POLICY] Loaded %d rules from disk", policy_rule_count);
    return 0;
}
```

### 6. policy_enforce.c — Runtime Enforcement

```c
// kernel/security/policy_enforce.c

int ai_kernel_engine_check_access(const char *cmd_word) {
    for (int i = 0; i < policy_rule_count; i++) {
        if (strncmp(policy_rules[i].command, cmd_word, 16) == 0) {
            return policy_rules[i].min_priv;
        }
    }
    return 255;  // ไม่มี rule = allow all
}

int policy_check_command(const char *cmd, int privilege) {
    // แยก first word จาก command string
    char cmd_word[32] = {0};
    sscan_first_word(cmd, cmd_word, sizeof(cmd_word));

    int min_priv = ai_kernel_engine_check_access(cmd_word);

    if (min_priv == 255) return POLICY_ALLOW;   // no rule
    if (privilege >= min_priv) return POLICY_ALLOW;

    return POLICY_DENY;
}
```

---

## โครงสร้างข้อมูล / API หลัก

### Default Rules สรุป

| Command | Level | min_priv | คำอธิบาย |
|---------|-------|----------|---------|
| `shutdown` | root | 100 | ปิดระบบ |
| `restart` | root | 100 | รีสตาร์ท |
| `test` | root | 100 | ทดสอบ kernel |
| `policy` | root | 100 | จัดการ policy |
| `ai` | admin | 50 | AI engine commands |
| `users` | admin | 50 | จัดการ users |
| `auditlog` | admin | 50 | ดู audit log |
| `log` | admin | 50 | ดู kernel log |
| `rm` | admin | 50 | ลบไฟล์ |
| `ls` | user | 0 | แสดงไฟล์ |
| `cat` | user | 0 | อ่านไฟล์ |
| `help` | user | 0 | แสดง help |
| *(อื่นๆ)* | — | 255 | allow all |

---

## ขั้นตอนการทำงาน

```
Boot time:
1. kernel_init() → policy_load_from_disk()
   → ATA PIO read sector 4096
   → parse VPOL header + rules
   → store ใน policy_rules[]

Runtime (per command):
2. ผู้ใช้พิมพ์ "shutdown"
3. cli_execute("shutdown")
4. policy_check_command("shutdown", session.privilege)
   └── ai_kernel_engine_check_access("shutdown") → 100
   └── session.privilege (50) < 100 → DENY

5. DENY → แสดง "Permission denied"
         → auditlog_record_deny(username, "shutdown", "insufficient privilege", tick)
         → klog("[POLICY] deny: shutdown by user (priv=50)")
         → ai_send_event("DENY", pid, "shutdown")

Hot reload (via AI engine):
6. REQ|5|Reload policy
   → PolicyManager.reload_policy()
   → Compile YAML → binary
   → CMD|0|POLICY|load|<hex>
   → Kernel: policy_load_from_memory(data)
   → Log: "[POLICY] Reloaded N rules"
```

---

## ผลลัพธ์

### ตัวอย่าง Policy Enforcement

```
vernis> shutdown
[POLICY] Permission denied: 'shutdown' requires privilege 100, you have 50
Access denied. This action has been logged.

vernis> ls
/boot  /etc  /home  /tmp

vernis[root]> shutdown
[POLICY] Allowed: 'shutdown' (privilege 100 >= 100)
Shutting down VernisOS...
```

### ตรวจสอบ Policy ผ่าน AI Engine

```
REQ|6|Check policy for shutdown
→ RESP|6|Command 'shutdown': min_privilege=100 (root only)
         Your privilege: 50 → DENIED

REQ|7|List all policy rules
→ RESP|7|Policy rules (14 total):
         shutdown → root (100)
         restart  → root (100)
         ai       → admin (50)
         ls       → user (0)
         ...

REQ|8|Reload policy
→ RESP|8|Policy reloaded: 14 rules compiled and applied
```

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 13 จะเพิ่ม **Authentication Layer** และ **UserDB** เข้ามาใน security stack:

- ระบบ login ด้วย SHA-256 password hashing
- UserRecord ที่เก็บ privilege level ต่อ user
- Policy enforcement + user auth ทำงานร่วมกัน
- Audit log ที่บันทึก deny events ทุกครั้ง

```
Phase 12: Policy rules กำหนด "ต้องมี privilege เท่าไร"
Phase 13: UserDB กำหนด "user นี้มี privilege เท่าไร"
          ทั้งสองรวมกัน = complete access control
```
