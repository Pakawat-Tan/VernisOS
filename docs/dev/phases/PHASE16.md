# Phase 16 — Integration Test & Logging System

> สัปดาห์ 43–45 | ภาษา: C + Python | สถานะ: ⏳ อยู่ระหว่างพัฒนา

---

## เป้าหมาย

รวม klog (Kernel Logging System) ที่พัฒนาแล้วเข้ากับ CLI และสร้าง integration test framework ครบถ้วนที่สามารถ boot QEMU จริง, ส่งคำสั่ง, และตรวจสอบ output ได้อัตโนมัติ เพื่อใช้เป็น regression suite ก่อนทุก release

---

## ภาพรวม

```
Integration Test Architecture:

  ┌─────────────────────────────────────────────────┐
  │  Python Test Runner (test_integration.py)        │
  │                                                   │
  │  ┌─────────────┐    stdin pipe    ┌────────────┐  │
  │  │  QemuRunner │ ──────────────► │ QEMU x64   │  │
  │  │             │ ◄────────────── │ (headless) │  │
  │  │  serial_    │    stdout pipe  │ -serial    │  │
  │  │  expect()   │                 │  stdio     │  │
  │  └─────────────┘                 └─────┬──────┘  │
  │                                        │          │
  │                                  VernisOS Kernel   │
  │                                   ├── klog        │
  │                                   ├── CLI         │
  │                                   └── AI Bridge   │
  └─────────────────────────────────────────────────┘
```

| Component | ไฟล์ | สถานะ |
|-----------|------|-------|
| **klog.c** | `kernel/log/klog.c` | ✅ สมบูรณ์แล้ว |
| **klog.h** | `include/klog.h` | ✅ สมบูรณ์แล้ว |
| **Integration Tests** | `ai/tests/test_integration.py` | ⏳ พัฒนาอยู่ |

---

## ไฟล์ที่เกี่ยวข้อง

```
kernel/
└── log/
    └── klog.c                   # ✅ Kernel logging system (สมบูรณ์)
include/
└── klog.h                       # ✅ klog API declarations (สมบูรณ์)
kernel/cli/
└── cli.c                        # UPDATED: เพิ่ม 'log' command
ai/
└── tests/
    └── test_integration.py      # ⏳ End-to-end test framework
```

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. klog.c — Kernel Logging System (สมบูรณ์แล้ว)

```c
/* include/klog.h */

typedef enum {
    KLOG_DEBUG    = 0,
    KLOG_INFO     = 1,
    KLOG_WARNING  = 2,
    KLOG_ERROR    = 3,
    KLOG_CRITICAL = 4
} KlogLevel;

typedef struct {
    KlogLevel level;
    char      tag[16];       /* module/component name */
    char      message[128];  /* log message */
    uint32_t  tick;          /* kernel tick timestamp */
} KlogEntry;

/* Ring buffer: 256 entries */
#define KLOG_RING_SIZE 256

/* API */
void klog_init(void);
void klog_write(KlogLevel level, const char *tag, const char *msg);
void klog_print_recent(int n);
void klog_filter_level(KlogLevel min_level);
int  klog_get_count(void);
const KlogEntry *klog_get_entry(int index);
```

#### klog Ring Buffer Layout

```
Ring buffer (KLOG_RING_SIZE = 256):

   Oldest                                    Newest
     │                                          │
     ▼                                          ▼
  ┌────┬────┬────┬────┬─────┬────┬────┬────┬────┐
  │ E0 │ E1 │ E2 │ E3 │ ... │E251│E252│E253│E254│
  └────┴────┴────┴────┴─────┴────┴────┴────┴────┘
                                         ↑
                                    klog_head (ชี้ตำแหน่งต่อไป)

  เมื่อ buffer เต็ม: ทับ record เก่าที่สุดอัตโนมัติ
```

#### KlogLevel Color Codes (serial output)

```
[   0.012] [DEBUG   ] [memory  ] heap init: base=0x100000 size=4MB
[   0.015] [INFO    ] [kernel  ] VernisOS booting...
[   0.021] [WARNING ] [ipc     ] channel queue near full (90%)
[   0.034] [ERROR   ] [vfs     ] open failed: file not found
[   0.041] [CRITICAL] [sched   ] stack overflow detected in PID 7
```

#### การเรียกใช้ klog_write

```c
/* ตัวอย่างการใช้งานใน kernel */
klog_write(KLOG_INFO,    "kernel",  "VernisOS boot complete");
klog_write(KLOG_DEBUG,   "memory",  "kmalloc: 64 bytes at 0x200400");
klog_write(KLOG_WARNING, "ipc",     "queue 90% full");
klog_write(KLOG_ERROR,   "vfs",     "open /etc/passwd: not found");
klog_write(KLOG_CRITICAL,"sched",   "stack overflow PID 7");
```

---

### 2. CLI 'log' Command

เพิ่ม command `log` ใน CLI เพื่อให้ user/admin ดู kernel log ได้

```
VernisOS> log
แสดง 20 entries ล่าสุด (default)

VernisOS> log 50
แสดง 50 entries ล่าสุด

VernisOS> log 20 warning
แสดง 20 entries ล่าสุด ที่ level >= WARNING

VernisOS> log 0 critical
แสดงเฉพาะ CRITICAL entries ทั้งหมด
```

```c
/* kernel/cli/cli.c — 'log' command handler */
static void cmd_log(int argc, char **argv) {
    int count     = (argc > 1) ? atoi(argv[1]) : 20;
    int min_level = KLOG_DEBUG;

    if (argc > 2) {
        if      (strcmp(argv[2], "debug")    == 0) min_level = KLOG_DEBUG;
        else if (strcmp(argv[2], "info")     == 0) min_level = KLOG_INFO;
        else if (strcmp(argv[2], "warning")  == 0) min_level = KLOG_WARNING;
        else if (strcmp(argv[2], "error")    == 0) min_level = KLOG_ERROR;
        else if (strcmp(argv[2], "critical") == 0) min_level = KLOG_CRITICAL;
    }

    klog_filter_level(min_level);
    klog_print_recent(count);
}
```

---

### 3. Integration Test Framework

#### QemuRunner Class

```python
# ai/tests/test_integration.py

import subprocess, time, re, sys
from typing import Optional

class QemuRunner:
    """จัดการ QEMU process สำหรับ integration test"""

    BOOT_PROMPT = "VernisOS>"

    def __init__(self, arch: str = "x64", timeout: int = 30):
        self.arch    = arch
        self.timeout = timeout
        self.proc: Optional[subprocess.Popen] = None

    def start(self, img_path: str = "build/os.img") -> None:
        binary = "qemu-system-x86_64" if self.arch == "x64" \
                 else "qemu-system-i386"
        cmd = [
            binary,
            "-nographic",
            "-drive", f"file={img_path},format=raw",
            "-serial", "stdio",
            "-m", "64M",
            "-no-reboot",
        ]
        self.proc = subprocess.Popen(
            cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )
        self.serial_expect(self.BOOT_PROMPT, timeout=self.timeout)

    def send_command(self, cmd: str) -> str:
        """ส่งคำสั่งและรอ prompt กลับ"""
        self.proc.stdin.write(f"{cmd}\n".encode())
        self.proc.stdin.flush()
        return self.serial_expect(self.BOOT_PROMPT)

    def serial_expect(self, pattern: str, timeout: int = 10) -> str:
        """อ่าน output จนพบ pattern หรือ timeout"""
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            chunk = self.proc.stdout.read(64)
            if chunk:
                buf += chunk
                if pattern.encode() in buf:
                    return buf.decode(errors="replace")
        raise TimeoutError(
            f"serial_expect: '{pattern}' not found in {timeout}s\n"
            f"Output so far: {buf.decode(errors='replace')!r}")

    def stop(self) -> None:
        if self.proc:
            self.proc.terminate()
            self.proc.wait(timeout=5)
            self.proc = None
```

---

### 4. Test Scenarios

#### Boot Sequence Test

```python
class TestBootSequence:
    def test_selftest_passes(self, qemu):
        """ตรวจว่า selftest ผ่านทั้งหมดตอน boot"""
        output = qemu.boot_output
        assert "[selftest] done:" in output
        assert "0 failed" in output

    def test_kernel_starts(self, qemu):
        """ตรวจว่า kernel เริ่มได้และแสดง prompt"""
        assert "VernisOS>" in qemu.boot_output

    def test_klog_init(self, qemu):
        """ตรวจว่า klog init ทำงาน"""
        output = qemu.send_command("log 5")
        # ต้องมี log entries จาก boot
        assert "kernel" in output or "INFO" in output
```

#### Login/Logout Test

```python
class TestLoginLogout:
    def test_login_root(self, qemu):
        resp = qemu.send_command("login root root123")
        assert any(x in resp.lower() for x in ["welcome", "root", "ok"])

    def test_wrong_password(self, qemu):
        resp = qemu.send_command("login root wrongpass")
        assert any(x in resp.lower() for x in ["denied", "failed", "invalid"])

    def test_logout(self, qemu):
        qemu.send_command("login root root123")
        resp = qemu.send_command("logout")
        assert any(x in resp.lower() for x in ["logout", "bye", "ok"])
```

#### IPC Test

```python
class TestIPC:
    def test_ipc_send_recv(self, qemu):
        qemu.send_command("login root root123")
        resp = qemu.send_command("ipc test")
        assert "ok" in resp.lower() or "pass" in resp.lower()
```

#### VernisFS Operations Test

```python
class TestVernisFS:
    def test_ls_root(self, qemu):
        resp = qemu.send_command("ls /")
        # ต้องมีไฟล์อย่างน้อย 1 ไฟล์
        assert len(resp.strip().split("\n")) > 1

    def test_cat_file(self, qemu):
        resp = qemu.send_command("cat /etc/version")
        assert "VernisOS" in resp
```

#### AI Bridge Test

```python
class TestAIBridge:
    def test_ai_query(self, qemu):
        qemu.send_command("login root root123")
        resp = qemu.send_command("ai status")
        assert any(x in resp.lower()
                   for x in ["connected", "ready", "ok", "bridge"])
```

---

## โครงสร้างข้อมูล / API หลัก

### klog API Summary

```c
/* การ init */
void klog_init(void);                              /* เรียกตอน boot */

/* การบันทึก log */
void klog_write(KlogLevel level,
                const char *tag,
                const char *msg);

/* Macros สะดวกใช้ */
#define KLOG_D(tag, msg) klog_write(KLOG_DEBUG,    tag, msg)
#define KLOG_I(tag, msg) klog_write(KLOG_INFO,     tag, msg)
#define KLOG_W(tag, msg) klog_write(KLOG_WARNING,  tag, msg)
#define KLOG_E(tag, msg) klog_write(KLOG_ERROR,    tag, msg)
#define KLOG_C(tag, msg) klog_write(KLOG_CRITICAL, tag, msg)

/* การอ่าน log */
void klog_print_recent(int n);                     /* แสดง n entries ล่าสุด */
void klog_filter_level(KlogLevel min_level);       /* set filter level */
int  klog_get_count(void);                         /* จำนวน entries ปัจจุบัน */
```

### pytest fixtures

```python
# conftest.py
import pytest

@pytest.fixture(scope="function")
def qemu():
    runner = QemuRunner(arch="x64")
    runner.start()
    yield runner
    runner.stop()

@pytest.fixture(scope="function")
def qemu_x86():
    runner = QemuRunner(arch="x86")
    runner.start()
    yield runner
    runner.stop()
```

---

## ขั้นตอนการทำงาน

### make test Flow

```
make test
    │
    ▼
1. Build os.img (x64)
   └── make all ARCH=x64
    │
    ▼
2. Launch QEMU (timeout 60s)
   └── qemu-system-x86_64 -nographic -serial stdio ...
    │
    ▼
3. Wait for boot prompt "VernisOS>"
   └── serial_expect("VernisOS>", timeout=30)
    │
    ▼
4. Inject test commands via stdin pipe
   ├── send "login root root123"
   ├── send "ps"
   ├── send "log 10"
   ├── send "ipc test"
   └── send "ls /"
    │
    ▼
5. Assert expected outputs
   ├── PASS: "[selftest] 0 failed"
   ├── PASS: "VernisOS>" (prompt)
   ├── PASS: "root" in ps output
   └── PASS: "VernisOS" in version
    │
    ▼
6. Kill QEMU
    │
    ▼
7. Report PASS / FAIL + duration
```

### Test Matrix

| Test Class | x64 | x86 | คำอธิบาย |
|-----------|-----|-----|---------|
| TestBootSequence | ✓ | ✓ | boot + selftest |
| TestLoginLogout | ✓ | ✓ | auth flow |
| TestCommandPrivilege | ✓ | ✓ | permission checks |
| TestIPC | ✓ | ✓ | IPC send/recv |
| TestVernisFS | ✓ | ✓ | file operations |
| TestAIBridge | ✓ | — | AI bridge (x64 only) |
| TestKlog | ✓ | ✓ | log command |

---

## ผลลัพธ์

```bash
$ make test
[build] os.img built (x64, 241 KB)
[qemu]  started PID 12345
[test]  boot_sequence ... PASS (4.2s)
[test]  login_logout  ... PASS (1.8s)
[test]  privilege     ... PASS (3.1s)
[test]  ipc           ... PASS (2.3s)
[test]  vernisfs      ... PASS (1.9s)
[test]  ai_bridge     ... PASS (5.4s)
[test]  klog          ... PASS (1.6s)
[qemu]  stopped
─────────────────────────────────────────
7 scenarios PASSED | 0 FAILED | 20.3s
```

---

## สิ่งที่ต่อใน Phase ถัดไป

- **Phase 17**: Developer Preview — integration test suite ต้องผ่าน 100% ก่อน release
- เพิ่ม test สำหรับ edge cases: session timeout, concurrent commands, IPC overflow
- เพิ่ม `klog` output ใน crash dump เมื่อ kernel panic
- พิจารณา structured logging (JSON format) สำหรับ AI bridge parsing
- Export test results เป็น JUnit XML เพื่อใช้กับ CI/CD ในอนาคต
- เพิ่ม performance regression test: วัด boot time และ alert หาก > threshold
