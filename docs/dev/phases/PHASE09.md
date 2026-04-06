# Phase 9 — Python AI Engine: Corelib + Listener
> สัปดาห์ 22–24 | ภาษา: Python 3.12 | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

พัฒนา **Python AI Engine** ที่ทำหน้าที่เป็น Intelligent Listener สำหรับ VernisOS โดยรับ Event จาก Kernel ผ่าน COM2 Bridge (TCP 4444), ประมวลผลด้วยระบบ Plugin Modules, และสามารถส่งคำสั่งกลับไปยัง Kernel ได้ผ่านช่องทางเดิม

---

## ภาพรวม

```
                    Python AI Engine Architecture
┌─────────────────────────────────────────────────────────────────┐
│                        ai_listener.py                           │
│  (Entry point — โหลด modules, connect TCP, เริ่ม run_loop)       │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                         AIEngine (corelib.py)                   │
│                                                                 │
│  connect(host, port)      run_loop()                            │
│  ├─ TCP socket            ├─ อ่าน byte stream จาก COM2          │
│  └─ recv/send thread      ├─ parse frame (TYPE|f1|f2\n)        │
│                           ├─ EVT → dispatch_event(evt)          │
│  send_request(query)      └─ CMD response matching             │
│  ├─ format REQ frame                                            │
│  ├─ wait RESP (seq match)  register_module(module)             │
│  └─ return response        └─ sorted by priority               │
└───────────────────────────┬─────────────────────────────────────┘
                            │ dispatch_event()
              ┌─────────────┼─────────────┐
              ▼             ▼             ▼
     ┌────────────────┐  ┌──────────┐  ┌──────────────────────┐
     │ system_monitor │  │  (future │  │  (future modules...) │
     │    .py         │  │  modules)│  │                      │
     │ handle STAT    │  └──────────┘  └──────────────────────┘
     │ track metrics  │
     └────────────────┘
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | บทบาท |
|------|--------|
| `ai/corelib.py` | AIEngine class — TCP connect, frame I/O, module dispatch |
| `ai/ai_listener.py` | Entry point — โหลด modules, รัน engine |
| `ai/modules/base.py` | AIModule abstract base class |
| `ai/modules/system_monitor.py` | Module ติดตาม STAT events จาก Kernel |

**Dependencies:** Python 3.12 standard library เท่านั้น (ไม่มี third-party ใน Phase 9)
`ruamel.yaml` จะเพิ่มใน Phase 12 สำหรับ Policy YAML parsing

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. AIModule Base Class (modules/base.py)

```python
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Optional

@dataclass
class KernelEvent:
    event_type: str   # BOOT, PROC, MOD, EXCP, DENY, FAIL, STAT, SYSCALL
    data: str         # Payload string จาก EVT frame

class AIModule(ABC):
    """Base class สำหรับทุก AI Module"""

    name: str = "unnamed"
    priority: int = 100   # ตัวเลขน้อย = priority สูง (รับก่อน)

    @abstractmethod
    def handle_event(self, evt: KernelEvent) -> None:
        """รับ Kernel Event และประมวลผล"""
        ...

    def handle_query(self, query: str) -> Optional[str]:
        """รับ Query จาก Kernel และส่งคำตอบกลับ (optional)"""
        return None

    def on_connect(self) -> None:
        """เรียกเมื่อ connect กับ Kernel สำเร็จ"""
        pass

    def on_disconnect(self) -> None:
        """เรียกเมื่อ disconnect"""
        pass
```

### 2. AIEngine (corelib.py)

```python
import socket
import threading
from typing import List, Optional
from .modules.base import AIModule, KernelEvent

class AIEngine:
    def __init__(self):
        self._sock: Optional[socket.socket] = None
        self._modules: List[AIModule] = []
        self._seq: int = 0
        self._pending_resps: dict = {}   # seq → Event (threading)
        self._lock = threading.Lock()

    def connect(self, host: str = "localhost", port: int = 4444) -> bool:
        """เชื่อมต่อ TCP ไปยัง QEMU COM2 Bridge"""
        try:
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._sock.connect((host, port))
            self._sock.settimeout(0.1)
            return True
        except OSError as e:
            print(f"[AIEngine] connect failed: {e}")
            return False

    def register_module(self, module: AIModule) -> None:
        """ลงทะเบียน Module และ sort ตาม priority"""
        self._modules.append(module)
        self._modules.sort(key=lambda m: m.priority)

    def send_request(self, query: str) -> Optional[str]:
        """ส่ง REQ frame และรอ RESP จาก Kernel"""
        with self._lock:
            seq = self._seq
            self._seq += 1

        frame = f"REQ|{seq:04d}|{query}\n"
        self._sock.sendall(frame.encode())

        # รอ RESP ที่ตรง seq (timeout 5 วินาที)
        evt = threading.Event()
        self._pending_resps[seq] = (evt, None)
        if evt.wait(timeout=5.0):
            _, response = self._pending_resps.pop(seq)
            return response
        self._pending_resps.pop(seq, None)
        return None   # Timeout

    def dispatch_event(self, evt: KernelEvent) -> None:
        """ส่ง Event ไปยังทุก Module ตาม priority"""
        for module in self._modules:
            try:
                module.handle_event(evt)
            except Exception as e:
                print(f"[{module.name}] handle_event error: {e}")

    def run_loop(self) -> None:
        """อ่าน byte stream จาก COM2 และ parse frames"""
        buf = ""
        while True:
            try:
                data = self._sock.recv(256).decode(errors="replace")
                if not data:
                    break
                buf += data
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    self._parse_frame(line.strip())
            except socket.timeout:
                continue
            except OSError:
                break
```

### 3. Frame Parser Logic

```python
    def _parse_frame(self, line: str) -> None:
        if not line:
            return
        parts = line.split("|", 3)
        frame_type = parts[0]

        if frame_type == "EVT" and len(parts) >= 3:
            evt = KernelEvent(event_type=parts[1], data=parts[2])
            self.dispatch_event(evt)

        elif frame_type == "RESP" and len(parts) >= 3:
            seq = int(parts[1])
            resp_text = parts[2]
            if seq in self._pending_resps:
                evt_obj, _ = self._pending_resps[seq]
                self._pending_resps[seq] = (evt_obj, resp_text)
                evt_obj.set()   # ปลดล็อก send_request() ที่รออยู่

        elif frame_type == "CMD" and len(parts) >= 4:
            self._handle_cmd(parts[1], parts[2], parts[3])
```

### 4. system_monitor.py — STAT Event Tracking

```python
from .base import AIModule, KernelEvent

class SystemMonitor(AIModule):
    name = "system_monitor"
    priority = 10   # Priority สูง รับก่อน modules อื่น

    def __init__(self):
        self.proc_count: int = 0
        self.memory_used_pct: int = 0
        self.ipc_queue_len: int = 0
        self._history: list = []   # เก็บ snapshot สูงสุด 60 รายการ

    def handle_event(self, evt: KernelEvent) -> None:
        if evt.event_type != "STAT":
            return

        key, _, value = evt.data.partition("|")
        val = int(value) if value.isdigit() else 0

        if key == "proc_count":
            self.proc_count = val
        elif key == "memory_used_pct":
            self.memory_used_pct = val
            if val > 90:
                print(f"[WARN] Memory critical: {val}%")
        elif key == "ipc_queue_len":
            self.ipc_queue_len = val

        self._history.append({
            "proc": self.proc_count,
            "mem":  self.memory_used_pct,
            "ipc":  self.ipc_queue_len,
        })
        if len(self._history) > 60:
            self._history.pop(0)
```

---

## โครงสร้างข้อมูล / API หลัก

### Module Priority Table

| Priority | Module | รับ Events |
|----------|--------|-----------|
| 10 | `system_monitor` | STAT |
| 20 | `security_monitor` (Phase 12) | DENY, EXCP |
| 50 | `process_tracker` (Phase 12) | PROC, MOD |
| 100 | `default_logger` | ทุก Type |

### KernelEvent Dataclass

```python
@dataclass
class KernelEvent:
    event_type: str   # "BOOT" | "PROC" | "MOD" | "EXCP" | "DENY" |
                      # "FAIL" | "STAT" | "SYSCALL"
    data: str         # Payload — format ขึ้นกับ event_type
```

---

## ขั้นตอนการทำงาน

### ai_listener.py — Entry Point

```python
#!/usr/bin/env python3
"""VernisOS AI Listener — เชื่อมต่อ Kernel ผ่าน COM2/TCP"""

import time
from ai.corelib import AIEngine
from ai.modules.system_monitor import SystemMonitor

def main():
    engine = AIEngine()

    # ลงทะเบียน Modules
    engine.register_module(SystemMonitor())
    # engine.register_module(SecurityMonitor())  # Phase 12

    # รอ QEMU เริ่มต้นก่อน connect
    print("[AI] Waiting for VernisOS kernel...")
    time.sleep(1)

    # Connect ไปยัง QEMU COM2 TCP Bridge
    if not engine.connect(host="localhost", port=4444):
        print("[AI] Cannot connect. Is QEMU running with make run64-ai?")
        return

    print("[AI] Connected to VernisOS kernel")

    # Notify modules
    for mod in engine._modules:
        mod.on_connect()

    # เริ่ม main loop — รับ frames จนกว่า QEMU จะปิด
    engine.run_loop()

    print("[AI] Disconnected")

if __name__ == "__main__":
    main()
```

### Startup Sequence

```
Terminal 1: make run64-ai
      │
      ▼
QEMU boot → Kernel init
      │
      ├─ ai_bridge_init()  → COM2 ready
      ├─ EVT|BOOT|vernisOS-x86_64
      └─ ────────────── TCP:4444 ──────────────
                                               │
Terminal 2: python3 ai/ai_listener.py          │
      │                                        │
      ├─ AIEngine.connect(localhost, 4444) ────┘
      ├─ register_module(SystemMonitor)
      └─ run_loop()
             │
             ├─ EVT|BOOT|...     → dispatch_event
             ├─ EVT|STAT|...     → system_monitor.handle_event
             ├─ EVT|DENY|...     → (future security_monitor)
             └─ [CMD|...|...]    → _handle_cmd
```

### ตัวอย่าง Session Output

```
[AI] Connected to VernisOS kernel
[BOOT] VernisOS x86_64 started
[STAT] proc=2 mem=14% ipc_q=0
[PROC] pid=3 name=shell type=system
[STAT] proc=3 mem=18% ipc_q=2
[DENY] pid=5 tried syscall=28 (MODULE_LOAD) — no capability
[WARN] Memory critical: 91%
```

---

## ผลลัพธ์

| รายการ | ผลลัพธ์ |
|--------|---------|
| AIEngine.connect() TCP | ✅ เชื่อมต่อ QEMU COM2 Bridge ที่ port 4444 |
| run_loop() Frame Parser | ✅ parse EVT/RESP/CMD ได้ถูกต้อง |
| send_request() + seq matching | ✅ รอ RESP ถูก seq พร้อม timeout 5 วินาที |
| register_module() Plugin System | ✅ sort ตาม priority |
| dispatch_event() | ✅ ส่ง event ไปทุก module ตามลำดับ |
| SystemMonitor — STAT tracking | ✅ track proc/mem/ipc, alert เมื่อ mem > 90% |
| History buffer (60 snapshots) | ✅ เก็บ 60 วินาทีล่าสุด |
| ไม่ต้องการ third-party libs | ✅ stdlib เท่านั้นใน Phase 9 |
| QEMU integration `make run64-ai` | ✅ ทดสอบ end-to-end ได้ทันที |

---

## สิ่งที่ต่อใน Phase ถัดไป

**Phase 10** (ตามที่บันทึกใน PHASE10.md) และ **Phase 12 — Policy Engine** จะขยาย AI Engine ด้วย:
- `SecurityMonitor` module รับ DENY/EXCP events และออก Alert
- `ProcessTracker` module ติดตาม PROC/MOD lifecycle
- `ruamel.yaml` สำหรับโหลด Security Policy YAML
- AIEngine.send_command() สำหรับส่ง CMD frame กลับ Kernel
- Interactive Query จาก CLI `ai <query>` → Python LLM → ตอบกลับใน Terminal
