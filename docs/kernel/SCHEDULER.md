# VernisOS — ตัวจัดกำหนดการ Process (Scheduler)

> ไฟล์ที่เกี่ยวข้อง: `kernel/core/verniskernel/src/scheduler.rs`

---

## ภาพรวม

Scheduler ของ VernisOS เขียนด้วย **Rust (no_std)** เพื่อความปลอดภัยในการจัดการ memory
และ export เป็น C FFI สำหรับ kernel C code ใช้งาน

- อัลกอริทึม: **Priority-based Preemptive Round-Robin**
- โครงสร้างข้อมูล: `BTreeMap<usize, ProcessControlBlock>` (ordered by PID)
- การ schedule จริง: เรียกจาก `kernel_main` ตั้งต้น, ยัง **ไม่มี context switch** ระหว่าง interrupt
- Process สูงสุด: ไม่จำกัดใน Rust (ขึ้นอยู่กับ heap)

---

## ProcessControlBlock (PCB)

```rust
pub struct ProcessControlBlock {
    pub pid: usize,
    pub ppid: Option<usize>,
    pub state: ProcessState,

    // Priority
    pub priority: u8,             // 0–139 (สูง = ค่าสูง)
    pub nice: i8,                 // -20 ถึง +19
    pub cached_effective_priority: u8,  // priority + nice*2, clamp 0–139

    // CPU Context (สำหรับ context switch ในอนาคต)
    pub context: CpuContext,      // rax, rbx, rcx, ... r15, rflags, rsp, rip

    // Memory
    pub memory_info: MemoryInfo,  // virt, rss, shared, text, data, stack, heap
    pub user_memory_base: usize,  // 0x1000000 (16 MB)
    pub user_memory_size: usize,  // 4 MB

    // Timing
    pub cpu_time: Duration,
    pub start_time: Instant,
    pub time_quantum: Duration,         // default: 100ms
    pub time_slice_remaining: Duration,

    // Identity
    pub command: String,           // ชื่อ process เช่น "init", "ai_engine"
    pub process_type: ProcessType, // Kernel / System / User
    pub privilege_ring: PrivilegeRing, // Ring0 / Ring1 / Ring2 / Ring3

    // Security
    pub capabilities: u64,         // bitmask สิทธิ์ที่อนุญาต
    pub capability_denials: u64,   // นับครั้งที่ถูกปฏิเสธ
}
```

---

## Process States

```
New ──→ Standby ──→ Running ──→ Standby (time slice หมด)
                         │
                         ├──→ Waiting (blocked on I/O)
                         │      └──→ Standby (wake up)
                         │
                         ├──→ Suspended (ถูก suspend)
                         │      └──→ Standby (resume)
                         │
                         └──→ Terminated ──→ Zombie (รอ parent reap)
```

---

## Priority System

Priority ที่ effective จริง:

```
effective_priority = priority + nice × 2  (clamp ไว้ที่ 0–139)
```

- `priority` สูงกว่า = ได้รับการ schedule ก่อน (ตรงข้ามกับ Linux)
- `nice` เพิ่ม/ลด priority ชั่วคราว: `-20` (ลด 40 คะแนน) ถึง `+19` (เพิ่ม 38 คะแนน)
- Kernel processes สร้างด้วย priority 100
- User processes สร้างด้วย priority 50

### ตัวอย่าง

| Process | priority | nice | effective |
|---------|---------|------|-----------|
| `init` (PID 1) | 100 | 0 | 100 |
| `ai_engine` (PID 2) | 90 | 0 | 90 |
| user shell | 50 | 0 | 50 |
| background task | 50 | 10 | 70 |

---

## Scheduling Algorithm

```
ทุกครั้งที่ scheduler_schedule() ถูกเรียก:

1. หา process ที่กำลัง Running อยู่
2. อัปเดต cpu_time ของ process นั้น
3. ถ้า time_slice_remaining หมด:
   - เปลี่ยนสถานะเป็น Standby
   - รีเซ็ต time_slice_remaining = time_quantum

4. สแกน BTreeMap หา Standby process ที่มี
   effective_priority สูงสุด
5. เปลี่ยน process นั้นเป็น Running
6. return PID ของ process ที่เลือก
```

> **หมายเหตุ**: ปัจจุบัน context switch (บันทึก/คืน register state) ยังไม่ได้ implement
> Process จึงไม่ได้วิ่ง concurrent จริง — scheduler เป็นแค่ priority queue ในตอนนี้

---

## C FFI Exports

ฟังก์ชันทั้งหมดที่ kernel C สามารถเรียกได้:

### การสร้างและจัดการ Scheduler

```c
void    *scheduler_new(void);
void     scheduler_free(void *sched);
```

### Process Lifecycle

```c
uint32_t scheduler_create_process(void *sched, uint8_t priority, const char *cmd);
uint32_t scheduler_create_user_process(void *sched, const char *cmd);
uint32_t scheduler_create_system_process(void *sched, const char *cmd);
uint32_t scheduler_schedule(void *sched);
void     scheduler_terminate_current(void *sched);
void     scheduler_kill_process(void *sched, uint32_t pid);
void     scheduler_block_current(void *sched);
void     scheduler_wake_process(void *sched, uint32_t pid);
void     scheduler_suspend_process(void *sched, uint32_t pid);
void     scheduler_resume_process(void *sched, uint32_t pid);
void     scheduler_cleanup_zombies(void *sched);
```

### การดึงข้อมูล

```c
uint32_t scheduler_get_process_count(const void *sched);
uint32_t scheduler_get_running_process_count(const void *sched);
uint32_t scheduler_get_standby_process_count(const void *sched);
uint32_t scheduler_get_waiting_process_count(const void *sched);
uint32_t scheduler_get_current_process(const void *sched);
void     scheduler_get_scheduler_stats(const void *sched, uint64_t *out);
void     scheduler_get_pid_list(const void *sched, uint32_t *out, uint32_t max);

// ดึง snapshot สำหรับ ps command
int      scheduler_get_ps_row(const void *sched, uint32_t pid, PsRow *out);
```

### การปรับแต่ง

```c
void scheduler_set_priority(void *sched, uint32_t pid, uint8_t priority);
void scheduler_set_quantum(void *sched, uint32_t quantum_ms);
void scheduler_set_nice(void *sched, uint32_t pid, int8_t nice);
```

### Security/Capability

```c
uint8_t  scheduler_get_process_privilege(const void *sched, uint32_t pid);
uint8_t  scheduler_get_process_type(const void *sched, uint32_t pid);
void     scheduler_grant_capability(void *sched, uint32_t pid, uint64_t cap_mask);
void     scheduler_revoke_capability(void *sched, uint32_t pid, uint64_t cap_mask);
bool     scheduler_has_capability(const void *sched, uint32_t pid, uint64_t cap_mask);
void     scheduler_get_user_memory_layout(const void *sched, uint32_t pid,
                                           uintptr_t *base, uintptr_t *size);
```

---

## PsRow — Compact Process Snapshot

โครงสร้างที่ kernel ส่งให้ CLI `ps` command:

```c
typedef struct {
    uint32_t pid;
    uint32_t ppid;
    uint8_t  state;     // 0=New 1=Standby 2=Running 3=Waiting 4=Suspended 5=Terminated 6=Zombie
    uint8_t  priority;
    uint8_t  ptype;     // 0=Kernel 1=System 2=User
    uint8_t  ring;      // 0=Ring0 1=Ring1 2=Ring2 3=Ring3
    uint64_t cpu_time_ms;
    uint64_t uptime_secs;
    uint64_t mem_rss;
    uint64_t mem_virt;
    uint8_t  command[32];
} PsRow;
```

---

## Capability Bitmask

| Bit | Capability | คำอธิบาย |
|-----|-----------|----------|
| 0 | `CAP_EXECUTE` | Execute สั่ง |
| 1 | `CAP_MEMORY_ALLOC` | จัดสรร memory |
| 2 | `CAP_IPC_SEND` | ส่ง IPC message |
| 3 | `CAP_IPC_RECV` | รับ IPC message |
| 4 | `CAP_MODULE_LOAD` | โหลด kernel module |
| 5 | `CAP_MODULE_UNLOAD` | ลบ kernel module |
| 6 | `CAP_FILESYSTEM` | เข้าถึง filesystem |
| 7 | `CAP_NETWORK` | เข้าถึง network (ยังไม่มี) |
| 8 | `CAP_DEVICE_IO` | I/O ports ตรงๆ |
| 9 | `CAP_PRIVILEGE_CHANGE` | เปลี่ยน privilege ตัวเอง |

Kernel processes ได้ทุก capability (`0xFFFFFFFFFFFFFFFF`)
User processes ได้เฉพาะ CAP_EXECUTE, CAP_MEMORY_ALLOC, CAP_IPC_SEND/RECV, CAP_FILESYSTEM

---

## AI Auto-Tuning Integration

Python AI Engine สามารถปรับ scheduler ผ่าน COM2 CMD frame:

```
CMD|1|TUNE|SCHED_QUANTUM|*|50|high_load
CMD|1|TUNE|SCHED_PRIORITY|*|120|ai_engine_boost
```

Kernel handler (`kernel_tune_handler` ใน kernel_x64.c):
```c
if (action == "SCHED_QUANTUM") {
    scheduler_set_quantum(scheduler, value);  // value = ms
}
if (action == "SCHED_PRIORITY") {
    scheduler_set_priority(scheduler, 1, value);  // PID 1, priority = value
}
```
