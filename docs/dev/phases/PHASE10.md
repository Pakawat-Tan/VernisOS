# Phase 10 — AI Behavior Monitor
> สัปดาห์ 25–27 | ภาษา: Python | สถานะ: ✅ เสร็จสมบูรณ์

---

## เป้าหมาย

พัฒนาระบบ **AI Behavior Monitor** ที่ตรวจจับพฤติกรรมผิดปกติของ process, module, และ kernel events แบบ real-time บน VernisOS โดยใช้ detection หลาย 3 ประเภท ได้แก่ Rate, Pattern, และ Threshold รวมถึงระบบ remediation ที่ส่ง CMD frames กลับ kernel เพื่อจัดการกับภัยคุกคามโดยอัตโนมัติ

---

## ภาพรวม

ระบบ Behavior Monitor ทำหน้าที่เป็น "สมอง" ของ AI engine ฝั่ง user-space โดยรับ kernel events ผ่าน COM2/TCP แล้ววิเคราะห์พฤติกรรมของระบบแบบ real-time เมื่อตรวจพบความผิดปกติจะส่งคำสั่ง remediation กลับไปยัง kernel เพื่อลดผลกระทบทันที

```
Kernel Events (via COM2/TCP)
         │
         ▼
  ┌─────────────────┐
  │   AIEngine      │  ← dispatches EVT frames
  │   ai_listener   │
  └────────┬────────┘
           │
           ▼
  ┌────────────────────────────┐
  │   BehaviorMonitorModule    │  priority=5
  │                            │
  │  ┌──────────────────────┐  │
  │  │   AnomalyDetector    │  │  Rate / Pattern / Threshold
  │  └──────────┬───────────┘  │
  │             │              │
  │  ┌──────────▼───────────┐  │
  │  │   ProcessTracker     │  │  Per-PID profiling + trust
  │  └──────────┬───────────┘  │
  │             │              │
  │  ┌──────────▼───────────┐  │
  │  │   EventStore         │  │  Ring buffer 2000 events
  │  └──────────┬───────────┘  │
  │             │              │
  │  ┌──────────▼───────────┐  │
  │  │  AlertDeduplicator   │  │  Suppress dup ภายใน 30s
  │  └──────────┬───────────┘  │
  │             │              │
  │  ┌──────────▼───────────┐  │
  │  │  ResponseHandler     │──┼──► CMD|REMEDIATE → Kernel
  │  └──────────────────────┘  │
  └────────────────────────────┘
```

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ประเภท | หน้าที่ |
|------|--------|---------|
| `ai/anomaly_detector.py` | Core | ตรวจจับ anomaly ด้วย rate/pattern/threshold rules |
| `ai/process_tracker.py` | Core | ติดตาม per-PID lifecycle + trust scoring |
| `ai/event_store.py` | Core | เก็บ event history + query API (ring buffer) |
| `ai/config_loader.py` | Util | โหลด detection rules จาก YAML config |
| `ai/response_handler.py` | Action | ส่ง remediation CMD frames กลับ kernel |
| `ai/alert_deduplicator.py` | Util | กรอง duplicate alerts ภายใน 30s window |
| `ai/modules/behavior_monitor.py` | Module | รวมทุก component เข้ากับ AI module system |
| `ai/config/anomaly_rules.yaml` | Config | YAML rules ที่แก้ไขได้โดยไม่ต้องแก้โค้ด |
| `ai/tests/test_phase10.py` | Test | 56 test cases ครอบคลุมทุก component |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. AnomalyDetector — ระบบตรวจจับ 3 ประเภท

**Rate Rules** — ตรวจสอบความถี่ของ event ในช่วงเวลาที่กำหนด

| Event | Max Count | Window | Severity | คำอธิบาย |
|-------|-----------|--------|----------|---------|
| EXCP | 5 | 10s | HIGH | Exception เยอะผิดปกติ |
| PROC | 20 | 5s | MEDIUM | Fork bomb สงสัย |
| MOD | 10 | 30s | MEDIUM | Module load storm |
| FAIL | 3 | 10s | HIGH | Repeated failures |
| DENY | 5 | 10s | MEDIUM | Access denial spam |

**Pattern Rules** — ตรวจสอบลำดับ event ที่น่าสงสัย

| Pattern Name | Sequence | Window | Severity |
|-------------|----------|--------|----------|
| exception-storm | EXCP × 3 | 3s | CRITICAL |
| module-crash | MOD → EXCP | 2s | HIGH |
| privilege-fail | DENY × 3 | 5s | HIGH |
| fork-crash | PROC × 2 + EXCP | 5s | HIGH |

**Threshold Rules** — ตรวจสอบค่าตัวเลขเกิน limit

| Metric | Max | Severity | คำอธิบาย |
|--------|-----|----------|---------|
| proc_count | 32 | MEDIUM | Process มากเกินไปในระบบ |
| module_count | 8 | LOW | Module โหลดเยอะเกินไป |
| ipc_queue_len | 100 | MEDIUM | IPC queue backlog สะสม |
| memory_used_pct | 90 | HIGH | หน่วยความจำใกล้เต็ม |

### 2. ProcessTracker — ติดตาม PID และ Trust Level

```python
@dataclass
class ProfileRecord:
    pid: int
    name: str
    parent_pid: int
    created_at: float
    exited_at: Optional[float]
    syscall_count: int = 0
    failure_count: int = 0
    denial_count: int = 0
    exception_count: int = 0
    anomaly_flags: set = field(default_factory=set)
    anomaly_count: int = 0
    trust: TrustLevel = TrustLevel.NORMAL
    _events: list = field(default_factory=list)  # max 32 entries
```

**Trust Level Escalation:**

```
TRUSTED ←── (ตั้งด้วยมือเท่านั้น)

NORMAL
  │  failures ≥ 3  OR  denials ≥ 2  OR  anomalies ≥ 2
  ▼
SUSPICIOUS
  │  failures ≥ 6  OR  denials ≥ 5  OR  anomalies ≥ 4
  ▼
UNTRUSTED  ──► อาจถูก throttle หรือ kill โดยอัตโนมัติ
```

### 3. EventStore — Ring Buffer 2000 Events

```python
store = EventStore(max_size=2000)

store.record("PROC", "10|fork|test_proc", source_pid=10)
store.query_by_type("EXCP", minutes=5)      # events ใน 5 นาที
store.query_by_pid(10, minutes=10)          # events ของ PID 10
store.count_by_type(minutes=5)             # นับแยกตาม type
store.summary()                             # สรุป human-readable
```

### 4. AlertDeduplicator — ป้องกัน Alert Spam

```
key = "{detector}:{title}"

เช่น: "rate:Exception storm detected"

Alert แรก  → ✅ ส่ง
ซ้ำใน 30s  → ❌ suppress (เพิ่ม suppressed_count)
หลัง 30s   → ✅ ส่งใหม่ได้
```

### 5. ResponseHandler — Remediation Actions

| Severity | Default Action | พฤติกรรม |
|----------|---------------|---------|
| CRITICAL | **kill** | ยุติ process ทันที |
| HIGH | **throttle** | ลด scheduler quantum → 25ms |
| MEDIUM | **log** | บันทึก log เท่านั้น |
| LOW | **log** | บันทึก log เท่านั้น |

---

## โครงสร้างข้อมูล / API หลัก

### Kernel Event Types

| Event | Format | ความหมาย |
|-------|--------|---------|
| BOOT | `BOOT\|reason` | System startup |
| PROC | `PROC\|pid\|action\|name` | Process fork/exec/exit |
| MOD | `MOD\|module_name\|action` | Module load/unload |
| EXCP | `EXCP\|code\|addr\|pid` | CPU exception |
| DENY | `DENY\|pid\|reason` | Access denied |
| FAIL | `FAIL\|pid\|reason` | Operation failed |
| STAT | `STAT\|metric\|value` | Numeric metric |
| SYSCALL | `SYSCALL\|pid\|num` | Syscall invocation |

### CMD|REMEDIATE Protocol

```
CMD|0|REMEDIATE|<action>|<target_pid>|<param>

ตัวอย่าง:
CMD|0|REMEDIATE|log|all|0              # Log only
CMD|0|REMEDIATE|throttle|15|25        # Throttle PID 15 → 25ms quantum
CMD|0|REMEDIATE|kill|15|0             # Kill PID 15
CMD|0|REMEDIATE|revoke|15|CAP_IPC_SEND # Revoke capability
CMD|0|REMEDIATE|suspend|15|0          # Suspend scheduling
```

### YAML Configuration

```yaml
# ai/config/anomaly_rules.yaml
rate_rules:
  - event_type: EXCP
    max_count: 5
    window_sec: 10.0
    severity: HIGH
    title: "Exception storm detected"
  - event_type: PROC
    max_count: 20
    window_sec: 5.0
    severity: MEDIUM
    title: "Rapid process creation"

pattern_rules:
  - name: exception-storm
    sequence: [EXCP, EXCP, EXCP]
    window_sec: 3.0
    severity: CRITICAL
    title: "Exception storm pattern"
  - name: module-crash
    sequence: [MOD, EXCP]
    window_sec: 2.0
    severity: HIGH
    title: "Module load followed by crash"

threshold_rules:
  - metric: proc_count
    max_val: 32
    severity: MEDIUM
    title: "Too many active processes"
  - metric: memory_used_pct
    max_val: 90
    severity: HIGH
    title: "Memory near capacity"
```

---

## ขั้นตอนการทำงาน

```
1. Kernel ส่ง EVT frame → AIEngine รับ
         │
         ▼
2. BehaviorMonitorModule.handle_event(event_type, payload)
         │
         ▼
3. EventStore.record(event_type, payload, pid)   ← เก็บทุก event
         │
         ▼
4. ProcessTracker.update(event_type, pid, action) ← update profile
         │
         ▼
5. AnomalyDetector.check_all(event_type, payload, stats)
    ├── check_rate_rules()    ← นับ events ใน window
    ├── check_pattern_rules() ← จับ sequence
    └── check_threshold_rules() ← เทียบ STAT values
         │
         ▼
6. มี Anomaly?
    ├── AlertDeduplicator.should_emit(key) → ส่งหรือ suppress
    │
    └── ResponseHandler.handle(alert, pid)
             │
             ├── CRITICAL → kill → CMD|0|REMEDIATE|kill|PID|0
             ├── HIGH     → throttle → CMD|0|REMEDIATE|throttle|PID|25
             └── MEDIUM/LOW → log
```

---

## ผลลัพธ์

ตัวอย่าง query ผ่าน AI Engine:

```
REQ|1|Show behavior status
→ RESP|1|Anomalies: 3 total — CRITICAL:0 HIGH:1 MEDIUM:2 LOW:0
         Processes: 5 active, 1 suspicious
         Event store: 42 events in last 5min — PROC:20, EXCP:5, STAT:17

REQ|2|Show recent anomalies
→ RESP|2|Last 3 anomalies:
           [HIGH]   Exception storm detected (PID 15)
           [MEDIUM] Rapid process creation (PID 7)
           [MEDIUM] IPC queue backlog (system)

REQ|3|PID 15 status
→ RESP|3|PID 15 (user_proc) [alive] trust=SUSPICIOUS
         syscalls=120 fails=4 denials=2 exceptions=3
         anomaly_flags: {exception_spam}
```

### Test Coverage

```bash
python3 -m pytest ai/tests/test_phase10.py -v
```

**56 tests** ครอบคลุม:
- AnomalyDetector: rate detection, pattern matching, threshold checks
- ProcessTracker: lifecycle tracking, trust level escalation
- EventStore: query API, ring buffer overflow, thread safety
- AlertDeduplicator: suppression window, reset behavior
- ConfigLoader: YAML loading, default values, apply rules
- BehaviorMonitorModule: integration, query handling, alert flow

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 11 จะนำข้อมูลที่ Behavior Monitor ตรวจพบมาใช้ใน **Auto-Tuning Engine**:

- เมื่อ Monitor ตรวจพบ load สูง → Tuner ปรับ scheduler quantum
- เมื่อ Monitor พบ memory ใกล้เต็ม → Tuner ลด memory threshold alert
- Python side ส่ง `CMD|TUNE` frames → Rust kernel side รับและ apply

```
BehaviorMonitor (Phase 10)
        │  anomaly/stats data
        ▼
AutoTuner (Phase 11)  ──► CMD|TUNE → kernel/ai/auto_tuner.rs
```
