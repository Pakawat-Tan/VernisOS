# Phase 14 — Test CLI-AI + Permission System

> สัปดาห์ 36–38 | ภาษา: C + Python | สถานะ: ⏳ อยู่ระหว่างพัฒนา

---

## เป้าหมาย

ตรวจสอบความถูกต้องของระบบ Permission ทั้งหมดผ่าน self-test suite ที่รันตอน boot และ integration test ที่รันบน QEMU จริง เพื่อให้มั่นใจว่า CLI, AI bridge, และ security layer ทำงานร่วมกันได้อย่างถูกต้องก่อนเข้าสู่ Phase Optimization

---

## ภาพรวม

Phase 14 แบ่งออกเป็น 2 ส่วนหลัก:

| ส่วน | ไฟล์ | ภาษา | หน้าที่ |
|------|------|------|---------|
| **Boot Self-Test** | `kernel/selftest/selftest.c` | C | รัน test suite ก่อน CLI init |
| **Audit Log** | `kernel/security/auditlog.c` | C | บันทึก deny events ลง ring buffer |
| **CLI Permission Tests** | `ai/tests/test_cli_permission.py` | Python | ทดสอบ permission ทุก role |
| **Integration Tests** | `ai/tests/test_integration.py` | Python | Boot QEMU + ส่งคำสั่งผ่าน serial |

```
Boot sequence พร้อม selftest:

  [BIOS/UEFI]
      │
      ▼
  [kernel_main()]
      │
      ├─► selftest_run_all()   ← Phase 14: รัน test suite ก่อน
      │       │
      │       ├── test_memory_alloc()
      │       ├── test_ipc_send_recv()
      │       ├── test_scheduler()
      │       ├── test_serial_write()
      │       └── test_vernisfs_open_read()
      │
      ├─► cli_init()           ← CLI เริ่มหลัง selftest ผ่าน
      ├─► ai_bridge_init()
      └─► scheduler_start()
```

---

## ไฟล์ที่เกี่ยวข้อง

```
kernel/
├── selftest/
│   └── selftest.c          # Boot-time self-test suite (NEW)
├── security/
│   └── auditlog.c          # Audit log ring buffer (NEW)
include/
├── selftest.h              # Selftest API declarations (NEW)
ai/
└── tests/
    ├── test_cli_permission.py   # Python permission tests (NEW)
    └── test_integration.py      # End-to-end QEMU tests (NEW)
```

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. selftest.c — Boot-time Self-Test Suite

รันโดยอัตโนมัติตอน kernel เริ่มต้น ก่อนที่ CLI จะ init ผล PASS/FAIL แสดงผ่าน serial port

```c
/* kernel/selftest/selftest.c */

#define SELFTEST_PASS  0
#define SELFTEST_FAIL -1

typedef int (*selftest_fn)(void);

typedef struct {
    const char *name;
    selftest_fn  fn;
} SelfTestEntry;

/* ตารางของ test functions ทั้งหมด */
static SelfTestEntry selftest_table[] = {
    { "memory_alloc",       test_memory_alloc       },
    { "ipc_send_recv",      test_ipc_send_recv       },
    { "scheduler_create",   test_scheduler_create    },
    { "serial_write",       test_serial_write        },
    { "vernisfs_open_read", test_vernisfs_open_read  },
    { NULL, NULL }
};

int selftest_run_all(void) {
    int passed = 0, failed = 0;
    for (int i = 0; selftest_table[i].name != NULL; i++) {
        int result = selftest_table[i].fn();
        if (result == SELFTEST_PASS) {
            serial_printf("[selftest] PASS: %s\n", selftest_table[i].name);
            passed++;
        } else {
            serial_printf("[selftest] FAIL: %s\n", selftest_table[i].name);
            failed++;
        }
    }
    serial_printf("[selftest] done: %d passed, %d failed\n", passed, failed);
    return (failed == 0) ? SELFTEST_PASS : SELFTEST_FAIL;
}
```

#### Test Functions แต่ละตัว

| Test | ทดสอบอะไร | ผ่านเมื่อ |
|------|-----------|----------|
| `test_memory_alloc` | kmalloc(64) + kfree | ptr != NULL และไม่ crash |
| `test_ipc_send_recv` | สร้าง channel, send, recv | message ตรงกัน |
| `test_scheduler_create` | สร้าง task dummy, schedule | task ถูกเพิ่มใน queue |
| `test_serial_write` | เขียน string ลง serial | ส่งครบทุก byte |
| `test_vernisfs_open_read` | open("/test"), read(4) | data ถูกต้อง |

```c
/* ตัวอย่าง test function */
static int test_memory_alloc(void) {
    void *ptr = kmalloc(64);
    if (!ptr) return SELFTEST_FAIL;
    /* เขียน pattern เพื่อตรวจ memory corruption */
    memset(ptr, 0xAB, 64);
    kfree(ptr);
    return SELFTEST_PASS;
}

static int test_ipc_send_recv(void) {
    ipc_channel_t ch;
    if (ipc_channel_create(&ch) != 0) return SELFTEST_FAIL;
    const char *msg = "SELFTEST";
    if (ipc_send(&ch, msg, 8) != 0) return SELFTEST_FAIL;
    char buf[8];
    if (ipc_recv(&ch, buf, 8) != 8) return SELFTEST_FAIL;
    return (memcmp(buf, msg, 8) == 0) ? SELFTEST_PASS : SELFTEST_FAIL;
}
```

---

### 2. auditlog.c — Audit Log Ring Buffer

บันทึกทุกครั้งที่คำสั่งถูกปฏิเสธ เก็บใน ring buffer 64 records ในหน่วยความจำ kernel

```c
/* kernel/security/auditlog.c */

#define AUDIT_RING_SIZE 64

typedef struct {
    char username[32];
    char command[32];
    char reason[32];
    uint32_t tick;          /* kernel tick ตอนเกิดเหตุ */
} AuditEntry;

static AuditEntry audit_ring[AUDIT_RING_SIZE];
static int audit_head = 0;
static int audit_count = 0;

void auditlog_record_deny(const char *username,
                          const char *command,
                          const char *reason) {
    AuditEntry *e = &audit_ring[audit_head % AUDIT_RING_SIZE];
    strncpy(e->username, username, 31);
    strncpy(e->command,  command,  31);
    strncpy(e->reason,   reason,   31);
    e->tick = kernel_tick_get();
    audit_head = (audit_head + 1) % AUDIT_RING_SIZE;
    if (audit_count < AUDIT_RING_SIZE) audit_count++;
}

void auditlog_print_all(void) {
    serial_printf("[audit] %d records:\n", audit_count);
    for (int i = 0; i < audit_count; i++) {
        AuditEntry *e = &audit_ring[i];
        serial_printf("  [%u] user=%s cmd=%s reason=%s\n",
                      e->tick, e->username, e->command, e->reason);
    }
}
```

```
Ring buffer (AUDIT_RING_SIZE = 64):

  Index:  0    1    2   ...  62   63
        ┌────┬────┬────┬───┬────┬────┐
        │ E0 │ E1 │ E2 │...│E62 │ E3 │  ← head wraps around
        └────┴────┴────┴───┴────┴────┘
                              ↑
                            audit_head (เมื่อ buffer เต็ม ทับ record เก่า)
```

---

### 3. test_cli_permission.py — Python Integration Tests

ทดสอบ permission model ของ CLI ผ่าน QEMU serial interface

```python
# ai/tests/test_cli_permission.py

class TestCLIPermissions:
    """ทดสอบระบบ permission ทุก role"""

    def test_root_only_denied_for_user(self, qemu):
        """คำสั่ง root-only ต้องถูกปฏิเสธสำหรับ user ทั่วไป"""
        qemu.login("user", "user123")
        response = qemu.send_command("shutdown")
        assert "denied" in response.lower() or "permission" in response.lower()

    def test_admin_commands_denied_for_user(self, qemu):
        """คำสั่ง admin ต้องถูกปฏิเสธสำหรับ user ทั่วไป"""
        qemu.login("user", "user123")
        for cmd in ["modload testmod", "modunload testmod", "auditlog"]:
            resp = qemu.send_command(cmd)
            assert "denied" in resp.lower(), f"ควรถูกปฏิเสธ: {cmd}"

    def test_user_commands_allowed(self, qemu):
        """คำสั่งพื้นฐานต้องใช้ได้สำหรับ user ทั่วไป"""
        qemu.login("user", "user123")
        for cmd in ["help", "version", "ps", "echo hello"]:
            resp = qemu.send_command(cmd)
            assert "denied" not in resp.lower(), f"ไม่ควรถูกปฏิเสธ: {cmd}"

    def test_login_logout_flow(self, qemu):
        """ทดสอบ flow การ login และ logout"""
        resp = qemu.send_command("login root root123")
        assert "welcome" in resp.lower() or "root" in resp.lower()
        resp = qemu.send_command("logout")
        assert "logout" in resp.lower() or "bye" in resp.lower()
```

---

### 4. test_integration.py — End-to-End Tests

บูต QEMU จริง แล้วส่งคำสั่งผ่าน serial stdin/stdout pipe

```python
# ai/tests/test_integration.py

import subprocess, time, re

class QemuRunner:
    def __init__(self, arch="x64"):
        self.arch = arch
        self.proc = None

    def start(self, img_path, timeout=30):
        cmd = ["qemu-system-x86_64", "-nographic",
               "-drive", f"file={img_path},format=raw",
               "-serial", "stdio", "-m", "64M"]
        if self.arch == "x86":
            cmd[0] = "qemu-system-i386"
        self.proc = subprocess.Popen(
            cmd, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self._wait_for("VernisOS>", timeout)

    def send_command(self, cmd):
        self.proc.stdin.write(f"{cmd}\n".encode())
        self.proc.stdin.flush()
        return self._read_until_prompt()

    def stop(self):
        if self.proc:
            self.proc.terminate()
            self.proc.wait()
```

---

## โครงสร้างข้อมูล / API หลัก

```c
/* include/selftest.h */

#define SELFTEST_PASS  0
#define SELFTEST_FAIL -1

/* รัน test ทั้งหมด — เรียกจาก kernel_main() ก่อน cli_init() */
int selftest_run_all(void);

/* รัน test เดียว ตาม name */
int selftest_run_one(const char *name);

/* แสดงรายชื่อ test ทั้งหมด */
void selftest_list(void);
```

```c
/* Audit Log API */

void auditlog_init(void);
void auditlog_record_deny(const char *user, const char *cmd, const char *reason);
void auditlog_print_all(void);
int  auditlog_get_count(void);
const AuditEntry *auditlog_get_entry(int index);
```

---

## ขั้นตอนการทำงาน

```
การรัน Test Suite ทั้งหมด:

  make test              make test-x86          make test-all
      │                      │                      │
      ▼                      ▼                      ▼
  Build os.img (x64)    Build os.img (x86)    รัน test + test-x86
      │                      │
      ▼                      ▼
  Launch QEMU x64        Launch QEMU x86
  -nographic             -nographic
  -serial stdio          -serial stdio
      │                      │
      ▼                      ▼
  inject test cmds      inject test cmds
  via stdin pipe        via stdin pipe
      │                      │
      ▼                      ▼
  assert expected       assert expected
  outputs               outputs
      │                      │
      ▼                      ▼
  Kill QEMU             Kill QEMU
  Report PASS/FAIL      Report PASS/FAIL
```

### Makefile Targets

```makefile
# Makefile (ส่วน test targets)

test:
	@python3 ai/tests/test_integration.py --arch x64

test-x86:
	@python3 ai/tests/test_integration.py --arch x86

test-all: test test-x86
	@echo "[test-all] DONE"

test-permissions:
	@python3 -m pytest ai/tests/test_cli_permission.py -v

test-selftest:
	@python3 -m pytest ai/tests/test_integration.py::TestSelftest -v
```

---

## ผลลัพธ์

Serial output ที่คาดหวังเมื่อ selftest ผ่าน:

```
[selftest] PASS: memory_alloc
[selftest] PASS: ipc_send_recv
[selftest] PASS: scheduler_create
[selftest] PASS: serial_write
[selftest] PASS: vernisfs_open_read
[selftest] done: 5 passed, 0 failed
[kernel] selftest OK — starting CLI
VernisOS> _
```

Python integration test output:

```
ai/tests/test_cli_permission.py::TestCLIPermissions::test_root_only_denied_for_user PASSED
ai/tests/test_cli_permission.py::TestCLIPermissions::test_admin_commands_denied_for_user PASSED
ai/tests/test_cli_permission.py::TestCLIPermissions::test_user_commands_allowed PASSED
ai/tests/test_cli_permission.py::TestCLIPermissions::test_login_logout_flow PASSED

4 passed in 12.3s
```

### สถานะปัจจุบัน

| ส่วน | สถานะ | หมายเหตุ |
|------|-------|---------|
| selftest.c | ✅ สร้างแล้ว | test ครบ 5 functions |
| selftest.h | ✅ สร้างแล้ว | API ครบ |
| auditlog.c | ✅ สร้างแล้ว | ring buffer 64 entries |
| test_cli_permission.py | ⏳ บางส่วน | ยังขาด edge cases |
| test_integration.py | ⏳ บางส่วน | QemuRunner พร้อม, test scenarios ยังไม่ครบ |

---

## สิ่งที่ต่อใน Phase ถัดไป

- **Phase 15**: Optimization — ใช้ผล selftest เป็น baseline วัด performance ก่อน/หลัง optimize
- เพิ่ม test cases สำหรับ edge cases ของ permission (เช่น session timeout, concurrent login)
- เพิ่ม `auditlog` CLI command เพื่อให้ admin ดู deny records ได้
- ผสาน selftest result เข้ากับ klog system (Phase 16)
- เพิ่ม regression test suite ที่รันอัตโนมัติก่อน build release
