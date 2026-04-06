# Phase 11 — AI Auto-Tuning Engine
> สัปดาห์ 28–29 | ภาษา: Python + Rust | สถานะ: ⏳ กำลังพัฒนา

---

## เป้าหมาย

พัฒนา **AI Auto-Tuning Engine** ที่ปรับพารามิเตอร์ kernel แบบ dynamic ตามสภาพโหลดของระบบ โดยอิงข้อมูลจาก BehaviorMonitor (Phase 10) ได้แก่ scheduler quantum, process priority, และ memory threshold โดยระบบทำงานร่วมกันระหว่าง Python (decision layer) กับ Rust kernel module (apply layer) ผ่าน CMD|TUNE protocol

---

## ภาพรวม

Auto-Tuning Engine แบ่งออกเป็น 2 ฝั่งที่ทำงานร่วมกัน:

```
┌─────────────────────────────────────────────────────────┐
│                   User Space (Python)                   │
│                                                         │
│  ┌───────────────────┐      ┌────────────────────────┐  │
│  │  BehaviorMonitor  │─────►│      AutoTuner         │  │
│  │   (Phase 10)      │stats │                        │  │
│  └───────────────────┘      │  tune_quantum()        │  │
│                             │  tune_priority()       │  │
│                             │  tune_memory_threshold │  │
│                             └───────────┬────────────┘  │
│                                         │ CMD|TUNE       │
└─────────────────────────────────────────┼───────────────┘
                                          │ COM2/TCP
┌─────────────────────────────────────────┼───────────────┐
│                Kernel Space (Rust)      │               │
│                                         ▼               │
│                             ┌────────────────────────┐  │
│                             │   ai/auto_tuner.rs     │  │
│                             │                        │  │
│                             │  TunerState            │  │
│                             │  apply_quantum()       │  │
│                             │  apply_priority_delta()│  │
│                             └───────────┬────────────┘  │
│                                         │ FFI calls      │
│                             ┌───────────▼────────────┐  │
│                             │   scheduler.rs         │  │
│                             │  scheduler_set_quantum │  │
│                             │  scheduler_adj_priority│  │
│                             └────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
```

**สถานะปัจจุบัน:** Python side พร้อมแล้ว — Rust kernel side ยังไม่ครบ (อยู่ระหว่าง implement FFI bindings)

---

## ไฟล์ที่เกี่ยวข้อง

| ไฟล์ | ภาษา | สถานะ | หน้าที่ |
|------|------|-------|---------|
| `ai/auto_tuner.py` | Python | ✅ พร้อม | Decision logic — คำนวณค่าที่ควรปรับ |
| `ai/modules/auto_tuner_module.py` | Python | ✅ พร้อม | AIModule wrapper, priority=3, handle STAT events |
| `kernel/core/verniskernel/src/ai/auto_tuner.rs` | Rust | ⏳ ยังไม่ครบ | Apply tuning parameters ผ่าน FFI |

---

## สิ่งที่พัฒนา (รายละเอียด)

### 1. AutoTuner (Python) — Decision Logic

**tune_quantum(load_pct):** ปรับ scheduler quantum ตาม CPU load

```python
def tune_quantum(self, load_pct: float) -> int:
    """
    ปรับ quantum ตาม CPU load percentage
    Returns: quantum ที่ควรใช้ (milliseconds)
    """
    if load_pct < 30:
        return 50    # load ต่ำ → quantum ใหญ่ = throughput ดี
    elif load_pct < 70:
        return 25    # load ปานกลาง → balanced
    else:
        return 10    # load สูง → quantum เล็ก = responsiveness ดี
```

```
load_pct < 30%   →  quantum = 50ms  (throughput mode)
30% ≤ load < 70% →  quantum = 25ms  (balanced mode)
load ≥ 70%       →  quantum = 10ms  (responsive mode)
```

**tune_priority(pid, anomaly_count):** ปรับ priority ตามพฤติกรรมของ process

```python
def tune_priority(self, pid: int, anomaly_count: int) -> int:
    """
    process ที่มี anomaly มาก → ลด priority
    process ที่ดี (anomaly น้อย) → เพิ่ม priority กลับ
    Returns: delta (-5 ถึง +3)
    """
    if anomaly_count >= 4:
        return -5    # ลด priority มาก
    elif anomaly_count >= 2:
        return -2    # ลด priority นิดหน่อย
    elif anomaly_count == 0:
        return +1    # process ดี → ให้รางวัลเล็กน้อย
    return 0
```

**tune_memory_threshold():** ปรับ alert level ตาม free memory

```python
def tune_memory_threshold(self) -> dict:
    """
    ปรับ threshold ตาม memory ที่เหลือ
    """
    free_pct = self._get_free_memory_pct()
    if free_pct < 10:
        return {"alert_level": "CRITICAL", "threshold": 95}
    elif free_pct < 20:
        return {"alert_level": "HIGH",     "threshold": 85}
    else:
        return {"alert_level": "MEDIUM",   "threshold": 75}
```

### 2. AutoTunerModule — AIModule Wrapper

```python
class AutoTunerModule(AIModule):
    priority = 3                    # รันก่อน module อื่น (BehaviorMonitor=5)
    handles = ["STAT"]             # สนใจเฉพาะ STAT events

    def handle_event(self, event_type, payload):
        if event_type == "STAT":
            metric, value = payload.split("|")[:2]
            if metric == "cpu_load":
                self._on_load_update(float(value))
            elif metric == "memory_used_pct":
                self._on_memory_update(float(value))
```

### 3. TunerState (Rust) — Kernel Side State

```rust
// kernel/core/verniskernel/src/ai/auto_tuner.rs

pub struct TunerState {
    pub current_quantum:    u32,           // milliseconds
    pub priority_table:     [i8; 64],      // delta per PID
    pub memory_threshold:   u8,            // percent
    pub quantum_changes:    u32,           // metric
    pub priority_adjustments: u32,         // metric
    pub memory_adjustments: u32,           // metric
}

impl TunerState {
    pub fn apply_quantum(&mut self, ms: u32) {
        self.current_quantum = ms;
        unsafe { scheduler_set_quantum(ms) };   // FFI
        self.quantum_changes += 1;
    }

    pub fn apply_priority_delta(&mut self, pid: u8, delta: i8) {
        self.priority_table[pid as usize] += delta;
        unsafe { scheduler_adjust_priority(pid, delta) };  // FFI
        self.priority_adjustments += 1;
    }
}
```

### 4. FFI Bindings (Rust ↔ C)

```rust
extern "C" {
    fn scheduler_set_quantum(ms: u32);
    fn scheduler_adjust_priority(pid: u8, delta: i8);
}
```

```c
// kernel/core/verniskernel/src/scheduler.c
void scheduler_set_quantum(uint32_t ms) {
    current_quantum_ms = ms;
    // update timer interrupt interval
}

void scheduler_adjust_priority(uint8_t pid, int8_t delta) {
    if (pid < MAX_PROCESSES && process_table[pid].active) {
        process_table[pid].priority =
            clamp(process_table[pid].priority + delta, 0, 255);
    }
}
```

---

## โครงสร้างข้อมูล / API หลัก

### CMD|TUNE Protocol

```
CMD|0|TUNE|<parameter>|<value>

ตัวอย่าง:
CMD|0|TUNE|quantum|50         # ตั้ง scheduler quantum = 50ms
CMD|0|TUNE|quantum|10         # ลด quantum เหลือ 10ms (high load)
CMD|0|TUNE|priority|15|-2     # ลด priority PID 15 ลง 2
CMD|0|TUNE|priority|7|+1      # เพิ่ม priority PID 7 ขึ้น 1
CMD|0|TUNE|mem_threshold|85   # ตั้ง memory alert threshold = 85%
```

### Metrics ที่ติดตาม

| Metric | ประเภท | คำอธิบาย |
|--------|--------|---------|
| `quantum_changes` | counter | จำนวนครั้งที่ปรับ quantum |
| `priority_adjustments` | counter | จำนวนครั้งที่ปรับ priority |
| `memory_adjustments` | counter | จำนวนครั้งที่ปรับ memory threshold |
| `last_quantum_ms` | value | quantum ปัจจุบัน (ms) |
| `last_load_pct` | value | CPU load ล่าสุด (%) |

---

## ขั้นตอนการทำงาน

```
1. Kernel ส่ง STAT event เช่น "STAT|cpu_load|72.5"
         │
         ▼
2. AutoTunerModule.handle_event("STAT", "cpu_load|72.5")
         │
         ▼
3. AutoTuner.tune_quantum(72.5)
   → load ≥ 70% → quantum = 10ms
         │
         ▼
4. ค่าเปลี่ยนจาก quantum เดิม?
   ├── ใช่ → ส่ง CMD|0|TUNE|quantum|10
   └── ไม่ใช่ → ข้าม (ลด noise)
         │
         ▼
5. Kernel Rust module รับ CMD|TUNE frame
   → parse → TunerState.apply_quantum(10)
   → FFI: scheduler_set_quantum(10)
         │
         ▼
6. Scheduler ใช้ quantum ใหม่ทันที
```

### Integration กับ BehaviorMonitor

```
BehaviorMonitor          AutoTuner
     │                       │
     │  anomaly detected      │
     │  trust=SUSPICIOUS      │
     │  for PID 15            │
     │                        │
     ├──── anomaly_count=3 ──►│
     │                        │  tune_priority(15, 3)
     │                        │  → delta = -2
     │                        │  → CMD|TUNE|priority|15|-2
     │                        │
     │  STAT|cpu_load|75      │
     ├────────────────────────►
     │                        │  tune_quantum(75)
     │                        │  → quantum = 10ms
     │                        │  → CMD|TUNE|quantum|10
```

---

## ผลลัพธ์

เมื่อ Phase 11 เสร็จสมบูรณ์ ระบบจะสามารถ:

| ความสามารถ | คำอธิบาย |
|----------|---------|
| Dynamic Quantum | ปรับ scheduler quantum อัตโนมัติ 10/25/50ms ตาม load |
| Behavior-based Priority | process ที่มีพฤติกรรมดีได้ priority สูงขึ้น |
| Memory-aware Alerting | ปรับ threshold alert ตาม memory ที่เหลืออยู่ |
| No-restart Tuning | ปรับได้ขณะ kernel กำลังรันอยู่โดยไม่ต้อง reboot |

### Query ตัวอย่าง

```
REQ|5|Show tuner status
→ RESP|5|AutoTuner status:
         Current quantum: 25ms
         Quantum changes: 7
         Priority adjustments: 12
         Memory adjustments: 3
         Last CPU load: 48.2%
         Last memory free: 34%
```

---

## สิ่งที่ต่อใน Phase ถัดไป

Phase 12 จะขยายด้าน **Policy** และ **Security Config**:

- นำ YAML config system ที่พัฒนาใน Phase 10 มาต่อยอดเป็น Policy Compiler
- สร้าง binary format `VPOL` สำหรับ access control rules
- ทำให้ kernel โหลด policy จาก disk sector โดยตรง

```
AutoTuner (Phase 11) ──► ปรับ performance
PolicySystem (Phase 12) ──► ควบคุม access control
    ทั้งสองทำงานใน AI engine layer เดียวกัน
```

**สิ่งที่ต้องทำให้เสร็จก่อนปิด Phase 11:**
- [ ] Implement `scheduler_set_quantum` FFI ใน Rust
- [ ] Implement `scheduler_adjust_priority` FFI ใน Rust
- [ ] เพิ่ม test coverage สำหรับ Rust side
- [ ] Integration test: Python → CMD|TUNE → Rust → FFI → C scheduler
