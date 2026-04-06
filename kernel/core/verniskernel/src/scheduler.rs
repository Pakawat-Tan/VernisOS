// scheduler.rs

use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use alloc::vec::Vec;
use alloc::boxed::Box;
use core::option::Option::{Some, None};
use core::result::Result::{Ok, Err};
use core::default::Default;
use core::ptr;
use core::time::Duration;
use core::ffi::c_char;

extern "C" {
    fn kernel_get_ticks() -> u32;
    fn kernel_get_timer_hz() -> u32;
}

#[inline(always)]
fn timer_hz() -> u64 {
    let hz = unsafe { kernel_get_timer_hz() } as u64;
    if hz == 0 { 100 } else { hz }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Instant(u64);
impl Instant {
    pub fn now() -> Self {
        let ticks = unsafe { kernel_get_ticks() } as u64;
        Instant(ticks)
    }
    pub fn duration_since(&self, earlier: Instant) -> Duration {
        let diff_ticks = self.0.saturating_sub(earlier.0);
        let hz = timer_hz() as u128;
        let nanos = (diff_ticks as u128)
            .saturating_mul(1_000_000_000u128)
            / hz;
        let nanos_u64 = if nanos > u64::MAX as u128 {
            u64::MAX
        } else {
            nanos as u64
        };
        Duration::from_nanos(nanos_u64)
    }
    pub fn elapsed(&self) -> Duration { Self::now().duration_since(*self) }
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProcessState {
    New,
    Standby,
    Running,
    Waiting,
    Suspended,
    Terminated,
    Zombie,
}

#[derive(Debug, Clone)]
pub struct CpuContext {
    // จำลอง register context สำหรับ x86_64
    pub rip: usize,    // Instruction Pointer
    pub rsp: usize,    // Stack Pointer
    pub rbp: usize,    // Base Pointer
    pub rax: usize,    // General Purpose Registers
    pub rbx: usize,
    pub rcx: usize,
    pub rdx: usize,
    pub rsi: usize,
    pub rdi: usize,
    pub r8: usize,
    pub r9: usize,
    pub r10: usize,
    pub r11: usize,
    pub r12: usize,
    pub r13: usize,
    pub r14: usize,
    pub r15: usize,
    pub rflags: usize, // Flags register
}

impl Default for CpuContext {
    fn default() -> Self {
        Self {
            rip: 0,
            rsp: 0,
            rbp: 0,
            rax: 0,
            rbx: 0,
            rcx: 0,
            rdx: 0,
            rsi: 0,
            rdi: 0,
            r8: 0,
            r9: 0,
            r10: 0,
            r11: 0,
            r12: 0,
            r13: 0,
            r14: 0,
            r15: 0,
            rflags: 0,
        }
    }
}

#[derive(Debug, Clone)]
pub struct MemoryInfo {
    pub virtual_memory_size: usize,  // Virtual memory size in bytes
    pub resident_set_size: usize,    // Physical memory usage in bytes
    pub shared_memory_size: usize,   // Shared memory size in bytes
    pub text_size: usize,           // Code segment size
    pub data_size: usize,           // Data segment size
    pub stack_size: usize,          // Stack size
    pub heap_size: usize,           // Heap size
}

impl Default for MemoryInfo {
    fn default() -> Self {
        Self {
            virtual_memory_size: 0,
            resident_set_size: 0,
            shared_memory_size: 0,
            text_size: 0,
            data_size: 0,
            stack_size: 0,
            heap_size: 0,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ProcessType {
    Kernel,   // Kernel process (pid=0)
    System,   // System services
    User,     // User application
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PrivilegeRing {
    Ring0 = 0,  // Kernel
    Ring1 = 1,  // Drivers
    Ring2 = 2,  // System
    Ring3 = 3,  // User
}

#[derive(Debug, Clone)]
pub struct ProcessControlBlock {
    pub pid: usize,
    pub ppid: Option<usize>,        // Parent PID
    pub state: ProcessState,
    pub priority: u8,               // Static priority (0-139, lower is higher priority)
    pub nice: i8,                   // Nice value (-20 to 19)
    pub context: CpuContext,
    pub memory_info: MemoryInfo,
    pub cpu_time: Duration,         // Total CPU time used
    pub start_time: Instant,        // When process was created
    pub last_run_time: Option<Instant>, // When process last ran
    pub time_quantum: Duration,     // Time quantum for this process
    pub time_slice_remaining: Duration, // Remaining time slice
    pub wait_reason: Option<String>, // Why process is waiting
    pub exit_code: Option<i32>,     // Exit code when terminated
    pub command: String,            // Command name
    pub working_directory: String,  // Current working directory
    
    // Phase 6: Sandbox & Security
    pub process_type: ProcessType,  // Kernel, System, or User
    pub privilege_ring: PrivilegeRing, // Execution privilege level
    pub capabilities: u64,          // Bitmask of allowed operations
    pub user_memory_base: usize,    // For user processes: heap/stack base
    pub user_memory_size: usize,    // For user processes: allocated size
    pub capability_denials: u64,    // Count of blocked operations
    pub cached_effective_priority: u8, // Cached: priority + nice*2 (Phase 15)
    
    // Phase 23: Signals
    pub signal_pending: u64,        // Bitmask of pending signals (bit N = signal N pending)
}

impl ProcessControlBlock {
    pub fn new(pid: usize, priority: u8, command: String) -> Self {
        Self {
            pid,
            ppid: None,
            state: ProcessState::New,
            priority,
            nice: 0,
            context: CpuContext::default(),
            memory_info: MemoryInfo::default(),
            cpu_time: Duration::ZERO,
            start_time: Instant::now(),
            last_run_time: None,
            time_quantum: Duration::from_millis(100), // Default 100ms quantum
            time_slice_remaining: Duration::from_millis(100),
            wait_reason: None,
            exit_code: None,
            command,
            working_directory: String::from("/"),
            
            // Phase 6: Default to kernel process (can be overridden)
            process_type: ProcessType::Kernel,
            privilege_ring: PrivilegeRing::Ring0,
            capabilities: 0xFFFFFFFFFFFFFFFF, // Full permissions by default
            user_memory_base: 0,
            user_memory_size: 0,
            capability_denials: 0,
            cached_effective_priority: priority,
            
            // Phase 23: No pending signals initially
            signal_pending: 0,
        }
    }

    pub fn get_effective_priority(&self) -> u8 {
        self.cached_effective_priority
    }

    /// Recompute and cache effective priority — call when priority or nice changes
    pub fn recompute_effective_priority(&mut self) {
        let base_priority = self.priority as i16;
        let nice_adjustment = (self.nice as i16) * 2;
        let effective = base_priority + nice_adjustment;
        self.cached_effective_priority = effective.max(0).min(139) as u8;
    }

    pub fn update_cpu_time(&mut self, duration: Duration) {
        self.cpu_time += duration;
    }

    pub fn reset_time_slice(&mut self) {
        self.time_slice_remaining = self.time_quantum;
    }

    // Phase 23: Signal handling
    pub fn signal_send(&mut self, sig: u8) {
        // Mark signal as pending (bit sig is set)
        if sig < 64 {
            self.signal_pending |= 1u64 << sig;
        }
    }

    pub fn get_pending_signal(&mut self) -> Option<u8> {
        // POSIX signals: highest-priority signals are delivered first
        // Priority order: SIGKILL (9), SIGTERM (15), SIGINT (2)
        if self.signal_pending & (1u64 << 9) != 0 {
            self.signal_pending &= !(1u64 << 9);
            return Some(9);
        }
        if self.signal_pending & (1u64 << 15) != 0 {
            self.signal_pending &= !(1u64 << 15);
            return Some(15);
        }
        if self.signal_pending & (1u64 << 2) != 0 {
            self.signal_pending &= !(1u64 << 2);
            return Some(2);
        }
        // Other signals (fallback to lowest pending)
        for sig in 0..64 {
            if self.signal_pending & (1u64 << sig) != 0 {
                self.signal_pending &= !(1u64 << sig);
                return Some(sig as u8);
            }
        }
        None
    }
}

pub struct Scheduler {
    pub processes: BTreeMap<usize, ProcessControlBlock>, // เปลี่ยนจาก HashMap เป็น BTreeMap
    pub current_pid: Option<usize>,
    pub next_pid: usize,            // Next available PID
    pub total_processes_created: usize,
    pub scheduler_start_time: Instant,
    pub cpu_usage: Duration,        // Total CPU time used by all processes
    pub context_switches: usize,    // Number of context switches
    pub idle_time: Duration,        // Total idle time
    pub last_schedule_time: Option<Instant>,
}

impl Scheduler {
    pub fn new() -> Self {
        Scheduler {
            processes: BTreeMap::new(), // เปลี่ยนจาก HashMap เป็น BTreeMap
            current_pid: None,
            next_pid: 1,
            total_processes_created: 0,
            scheduler_start_time: Instant::now(),
            cpu_usage: Duration::ZERO,
            context_switches: 0,
            idle_time: Duration::ZERO,
            last_schedule_time: None,
        }
    }

    pub fn create_process(&mut self, priority: u8, command: String) -> usize {
        let pid = self.next_pid;
        self.next_pid += 1;
        self.total_processes_created += 1;
        
        let mut proc = ProcessControlBlock::new(pid, priority, command);
        proc.state = ProcessState::Standby;
        
        self.processes.insert(pid, proc);
        pid
    }

    pub fn add_process(&mut self, mut proc: ProcessControlBlock) -> usize {
        let pid = proc.pid;
        // Only change to Standby if the process is not in Suspended or Zombie state
        if !matches!(proc.state, ProcessState::Suspended | ProcessState::Zombie) {
            proc.state = ProcessState::Standby;
        }
        self.processes.insert(pid, proc);
        pid
    }

    pub fn schedule(&mut self) -> Option<usize> {
        let now = Instant::now();
        
        // Update CPU usage for current process
        if let Some(current_pid) = self.current_pid {
            if let Some(current_proc) = self.processes.get_mut(&current_pid) {
                if let Some(last_run) = current_proc.last_run_time {
                    let cpu_time = now.duration_since(last_run);
                    current_proc.update_cpu_time(cpu_time);
                    self.cpu_usage += cpu_time;
                    
                    // Check if time slice expired
                    if cpu_time >= current_proc.time_slice_remaining {
                        current_proc.state = ProcessState::Standby;
                        current_proc.reset_time_slice();
                    }
                }
            }
        }

        // Find next process to run
        let mut next_pid = None;
        let mut highest_priority = 0;
    
        for (pid, proc) in &self.processes {
            if matches!(proc.state, ProcessState::Standby) {
                let effective_priority = proc.get_effective_priority();
                if effective_priority > highest_priority {
                    highest_priority = effective_priority;
                    next_pid = Some(*pid);
                }
            }
        }
    
        if let Some(pid) = next_pid {
            // Context switch
            if self.current_pid != Some(pid) {
                self.context_switches += 1;
                
                // Set current process to Standby if it was Running
                if let Some(current_pid) = self.current_pid {
                    if let Some(current_proc) = self.processes.get_mut(&current_pid) {
                        if matches!(current_proc.state, ProcessState::Running) {
                            current_proc.state = ProcessState::Standby;
                        }
                    }
                }
            }
    
            // Set new process to Running
            if let Some(next_proc) = self.processes.get_mut(&pid) {
                next_proc.state = ProcessState::Running;
                next_proc.last_run_time = Some(now);
                self.current_pid = Some(pid);
            }
        } else {
            // No process to run, update idle time
            if let Some(last_schedule) = self.last_schedule_time {
                self.idle_time += now.duration_since(last_schedule);
            }
            self.current_pid = None;
        }
        
        self.last_schedule_time = Some(now);
        self.current_pid
    }

    pub fn block_current(&mut self, reason: String) {
        if let Some(pid) = self.current_pid {
            if let Some(proc) = self.processes.get_mut(&pid) {
                proc.state = ProcessState::Waiting;
                proc.wait_reason = Some(reason);
            }
            self.current_pid = None;
        }
    }

    pub fn wake_process(&mut self, pid: usize) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            if matches!(proc.state, ProcessState::Waiting) {
                proc.state = ProcessState::Standby;
                proc.wait_reason = None;
                return true;
            }
        }
        false
    }

    pub fn terminate_current(&mut self, exit_code: i32) {
        if let Some(pid) = self.current_pid {
            if let Some(proc) = self.processes.get_mut(&pid) {
                proc.state = ProcessState::Terminated;
                proc.exit_code = Some(exit_code);
            }
            self.current_pid = None;
        }
    }

    pub fn kill_process(&mut self, pid: usize) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            proc.state = ProcessState::Terminated;
            proc.exit_code = Some(-1); // Killed
            return true;
        }
        false
    }

    pub fn suspend_process(&mut self, pid: usize) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            if matches!(proc.state, ProcessState::Running | ProcessState::Standby) {
                proc.state = ProcessState::Suspended;
                return true;
            }
        }
        false
    }

    pub fn resume_process(&mut self, pid: usize) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            if matches!(proc.state, ProcessState::Suspended) {
                proc.state = ProcessState::Standby;
                return true;
            }
        }
        false
    }

    pub fn set_priority(&mut self, pid: usize, priority: u8) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            proc.priority = priority;
            proc.recompute_effective_priority();
            return true;
        }
        false
    }

    pub fn set_nice(&mut self, pid: usize, nice: i8) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            proc.nice = nice.max(-20).min(19);
            proc.recompute_effective_priority();
            return true;
        }
        false
    }

    pub fn get_process_info(&self, pid: usize) -> Option<&ProcessControlBlock> {
        self.processes.get(&pid)
    }

    pub fn get_current_process(&self) -> Option<&ProcessControlBlock> {
        self.current_pid.and_then(|pid| self.processes.get(&pid))
    }

    pub fn get_process_count(&self) -> usize {
        self.processes.len()
    }

    pub fn get_running_process_count(&self) -> usize {
        self.processes.values().filter(|p| matches!(p.state, ProcessState::Running)).count()
    }

    pub fn get_standby_process_count(&self) -> usize {
        self.processes.values().filter(|p| matches!(p.state, ProcessState::Standby)).count()
    }

    pub fn get_waiting_process_count(&self) -> usize {
        self.processes.values().filter(|p| matches!(p.state, ProcessState::Waiting)).count()
    }

    pub fn get_scheduler_stats(&self) -> SchedulerStats {
        let uptime = self.scheduler_start_time.elapsed();
        let cpu_utilization = if uptime > Duration::ZERO {
            self.cpu_usage.as_secs_f64() / uptime.as_secs_f64() * 100.0
        } else {
            0.0
        };

        SchedulerStats {
            uptime,
            total_processes_created: self.total_processes_created,
            current_process_count: self.processes.len(),
            context_switches: self.context_switches,
            cpu_utilization,
            idle_time: self.idle_time,
            running_processes: self.get_running_process_count(),
            standby_processes: self.get_standby_process_count(),
            waiting_processes: self.get_waiting_process_count(),
        }
    }

    pub fn dump(&self) {
        // println!("=== Process Dump ===");
        // println!("Current PID: {:?}", self.current_pid);
        // println!("Total Processes: {}", self.processes.len());
        // for (pid, proc) in &self.processes {
        //     println!(
        //         "PID: {}, State: {:?}, Priority: {}, Nice: {}, Command: {}, CPU Time: {:.2}s",
        //         pid, proc.state, proc.get_effective_priority(), proc.nice, 
        //         proc.command, proc.cpu_time.as_secs_f64()
        //     );
        // }
    }

    pub fn cleanup_zombies(&mut self) -> usize {
        let mut removed_count = 0;
        let zombies: Vec<usize> = self.processes
            .iter()
            .filter(|(_, proc)| matches!(proc.state, ProcessState::Zombie))
            .map(|(pid, _)| *pid)
            .collect();
        
        for pid in zombies {
            self.processes.remove(&pid);
            removed_count += 1;
        }
        
        removed_count
    }

    // Phase 23: Signal handling
    pub fn signal_send(&mut self, dst_pid: usize, sig: u8) -> i32 {
        // Send signal to process
        if let Some(proc) = self.processes.get_mut(&dst_pid) {
            proc.signal_send(sig);
            0
        } else {
            -1
        }
    }

    pub fn get_pending_signal(&mut self, obj_pid: usize) -> i32 {
        // Get next pending signal from process (-1 if none)
        if let Some(proc) = self.processes.get_mut(&obj_pid) {
            if let Some(sig) = proc.get_pending_signal() {
                sig as i32
            } else {
                -1
            }
        } else {
            -1
        }
    }
}

#[derive(Debug, Clone)]
pub struct SchedulerStats {
    pub uptime: Duration,
    pub total_processes_created: usize,
    pub current_process_count: usize,
    pub context_switches: usize,
    pub cpu_utilization: f64,
    pub idle_time: Duration,
    pub running_processes: usize,
    pub standby_processes: usize,
    pub waiting_processes: usize,
}

// ================= FFI SECTION =================

// PsRow — compact process snapshot for CLI `ps` command
#[repr(C)]
pub struct PsRow {
    pub pid:      usize,
    pub ppid:     usize,
    pub state:    u8,     // 0=New 1=Standby 2=Running 3=Waiting 4=Suspended 5=Terminated 6=Zombie
    pub priority: u8,
    pub ptype:    u8,     // 0=Kernel 1=System 2=User
    pub ring:     u8,     // 0-3
    pub cpu_time_ms:  u64,    // Total CPU time in milliseconds
    pub mem_rss:      usize,  // Resident Set Size (bytes)
    pub mem_virt:     usize,  // Virtual memory size (bytes)
    pub uptime_secs:  u64,    // Seconds since process creation
    pub command:  [u8; 32],
}

/// Fill `pids_out[0..max]` with active PIDs. Returns actual count written.
#[no_mangle]
pub extern "C" fn scheduler_get_pid_list(
    sched: *const Scheduler,
    pids_out: *mut usize,
    max_count: usize,
) -> usize {
    if sched.is_null() || pids_out.is_null() || max_count == 0 {
        return 0;
    }
    let sched = unsafe { &*sched };
    let mut n = 0usize;
    for &pid in sched.processes.keys() {
        if n >= max_count { break; }
        unsafe { core::ptr::write(pids_out.add(n), pid); }
        n += 1;
    }
    n
}

/// Fill `out` with a PsRow for the given PID. Returns true on success.
#[no_mangle]
pub extern "C" fn scheduler_get_ps_row(
    sched: *const Scheduler,
    pid: usize,
    out: *mut PsRow,
) -> bool {
    if sched.is_null() || out.is_null() { return false; }
    let sched = unsafe { &*sched };
    let pcb = match sched.processes.get(&pid) {
        Some(p) => p,
        None => return false,
    };
    let row = unsafe { &mut *out };
    row.pid      = pcb.pid;
    row.ppid     = pcb.ppid.unwrap_or(0);
    row.state    = match pcb.state {
        ProcessState::New        => 0,
        ProcessState::Standby    => 1,
        ProcessState::Running    => 2,
        ProcessState::Waiting    => 3,
        ProcessState::Suspended  => 4,
        ProcessState::Terminated => 5,
        ProcessState::Zombie     => 6,
    };
    row.priority = pcb.priority;
    row.ptype    = match pcb.process_type {
        ProcessType::Kernel => 0,
        ProcessType::System => 1,
        ProcessType::User   => 2,
    };
    row.ring     = pcb.privilege_ring as u8;
    row.cpu_time_ms  = pcb.cpu_time.as_millis() as u64;
    row.mem_rss      = pcb.memory_info.resident_set_size;
    row.mem_virt     = pcb.memory_info.virtual_memory_size;
    row.uptime_secs  = pcb.start_time.elapsed().as_secs();
    let bytes = pcb.command.as_bytes();
    let n = bytes.len().min(31);
    row.command[..n].copy_from_slice(&bytes[..n]);
    row.command[n] = 0;
    true
}

#[repr(C)]
pub enum FfiProcessState {
    New,
    Standby,
    Running,
    Waiting,
    Suspended,
    Terminated,
    Zombie,
}

#[repr(C)]
pub struct FfiProcessInfo {
    pub pid: usize,
    pub state: FfiProcessState,
    pub priority: u8,
    pub nice: i8,
    pub cpu_time_secs: u64,
}

#[no_mangle]
pub extern "C" fn scheduler_new() -> *mut Scheduler {
    let sched = Box::new(Scheduler::new());
    Box::into_raw(sched)
}

#[no_mangle]
pub extern "C" fn scheduler_free(ptr: *mut Scheduler) {
    if !ptr.is_null() {
        unsafe { drop(Box::from_raw(ptr)); }
    }
}

#[no_mangle]
pub extern "C" fn scheduler_create_process(
    sched: *mut Scheduler,
    priority: u8,
    command: *const c_char,
) -> usize {
    if sched.is_null() || command.is_null() {
        return 0;
    }
    let sched = unsafe { &mut *sched };
    
    // Create CString from raw pointer safely
    let command_str = unsafe {
        let c_str = core::ffi::CStr::from_ptr(command);
        match c_str.to_str() {
            Ok(s) => s.to_string(),
            Err(_) => String::from("unknown"),
        }
    };
    
    sched.create_process(priority, command_str)
}

#[no_mangle]
pub extern "C" fn scheduler_schedule(sched: *mut Scheduler) -> usize {
    if sched.is_null() {
        return 0;
    }
    let sched = unsafe { &mut *sched };
    sched.schedule().unwrap_or(0)
}

#[no_mangle]
pub extern "C" fn scheduler_terminate_current(sched: *mut Scheduler, exit_code: i32) {
    if sched.is_null() {
        return;
    }
    let sched = unsafe { &mut *sched };
    sched.terminate_current(exit_code);
}

#[no_mangle]
pub extern "C" fn scheduler_get_process_info(
    sched: *const Scheduler,
    pid: usize,
    out_info: *mut FfiProcessInfo,
) -> bool {
    if sched.is_null() || out_info.is_null() {
        return false;
    }
    let sched = unsafe { &*sched };
    if let Some(proc) = sched.get_process_info(pid) {
        let info = FfiProcessInfo {
            pid: proc.pid,
            state: match proc.state {
                ProcessState::New => FfiProcessState::New,
                ProcessState::Standby => FfiProcessState::Standby,
                ProcessState::Running => FfiProcessState::Running,
                ProcessState::Waiting => FfiProcessState::Waiting,
                ProcessState::Suspended => FfiProcessState::Suspended,
                ProcessState::Terminated => FfiProcessState::Terminated,
                ProcessState::Zombie => FfiProcessState::Zombie,
            },
            priority: proc.priority,
            nice: proc.nice,
            cpu_time_secs: proc.cpu_time.as_secs(),
        };
        unsafe { ptr::write(out_info, info); }
        true
    } else {
        false
    }
}

// Phase 20: get exit code of a terminated process
#[no_mangle]
pub extern "C" fn scheduler_get_exit_code(sched: *const Scheduler, pid: usize) -> i32 {
    if sched.is_null() { return -1; }
    let sched = unsafe { &*sched };
    match sched.get_process_info(pid) {
        Some(pcb) => pcb.exit_code.unwrap_or(-1),
        None => -1,
    }
}

#[no_mangle]
pub extern "C" fn scheduler_block_current(sched: *mut Scheduler, reason: *const c_char) {
    if sched.is_null() || reason.is_null() {
        return;
    }
    let sched = unsafe { &mut *sched };
    
    let reason_str = unsafe {
        let c_str = core::ffi::CStr::from_ptr(reason);
        match c_str.to_str() {
            Ok(s) => s.to_string(),
            Err(_) => String::from("unknown"),
        }
    };
    
    sched.block_current(reason_str);
}

#[no_mangle]
pub extern "C" fn scheduler_wake_process(sched: *mut Scheduler, pid: usize) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &mut *sched };
    sched.wake_process(pid)
}

#[no_mangle]
pub extern "C" fn scheduler_suspend_process(sched: *mut Scheduler, pid: usize) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &mut *sched };
    sched.suspend_process(pid)
}

#[no_mangle]
pub extern "C" fn scheduler_resume_process(sched: *mut Scheduler, pid: usize) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &mut *sched };
    sched.resume_process(pid)
}

#[no_mangle]
pub extern "C" fn scheduler_kill_process(sched: *mut Scheduler, pid: usize) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &mut *sched };
    sched.kill_process(pid)
}

#[no_mangle]
pub extern "C" fn scheduler_get_process_count(sched: *const Scheduler) -> usize {
    if sched.is_null() { return 0; }
    let scheduler = unsafe { &*sched };
    scheduler.get_process_count()
}

#[no_mangle]
pub extern "C" fn scheduler_get_running_process_count(sched: *const Scheduler) -> usize {
    if sched.is_null() { return 0; }
    let scheduler = unsafe { &*sched };
    scheduler.get_running_process_count()
}

#[no_mangle]
pub extern "C" fn scheduler_get_standby_process_count(sched: *const Scheduler) -> usize {
    if sched.is_null() { return 0; }
    let scheduler = unsafe { &*sched };
    scheduler.get_standby_process_count()
}

#[no_mangle]
pub extern "C" fn scheduler_get_waiting_process_count(sched: *const Scheduler) -> usize {
    if sched.is_null() { return 0; }
    let scheduler = unsafe { &*sched };
    scheduler.get_waiting_process_count()
}

#[no_mangle]
pub extern "C" fn scheduler_get_scheduler_stats(sched: *const Scheduler, out_stats: *mut SchedulerStats) -> bool {
    if sched.is_null() || out_stats.is_null() { return false; }
    let scheduler = unsafe { &*sched };
    let stats = scheduler.get_scheduler_stats();
    unsafe { ptr::write(out_stats, stats); }
    true
}

#[no_mangle]
pub extern "C" fn scheduler_cleanup_zombies(sched: *mut Scheduler) -> usize {
    if sched.is_null() { return 0; }
    let scheduler = unsafe { &mut *sched };
    scheduler.cleanup_zombies()
}

#[no_mangle]
pub extern "C" fn scheduler_set_priority(sched: *mut Scheduler, pid: usize, priority: u8) -> bool {
    if sched.is_null() { return false; }
    let scheduler = unsafe { &mut *sched };
    scheduler.set_priority(pid, priority)
}

#[no_mangle]
pub extern "C" fn scheduler_set_quantum(sched: *mut Scheduler, quantum_ms: u32) {
    if sched.is_null() { return; }
    let scheduler = unsafe { &mut *sched };
    let q = Duration::from_millis(quantum_ms as u64);
    for proc in scheduler.processes.values_mut() {
        proc.time_quantum = q;
        proc.time_slice_remaining = q;
    }
}

#[no_mangle]
pub extern "C" fn scheduler_set_nice(sched: *mut Scheduler, pid: usize, nice: i8) -> bool {
    if sched.is_null() { return false; }
    let scheduler = unsafe { &mut *sched };
    scheduler.set_nice(pid, nice)
}

#[no_mangle]
pub extern "C" fn scheduler_get_current_process(sched: *const Scheduler) -> *const ProcessControlBlock {
    if sched.is_null() { return ptr::null(); }
    let scheduler = unsafe { &*sched };
    match scheduler.get_current_process() {
        Some(proc) => proc as *const ProcessControlBlock,
        None => ptr::null(),
    }
}

#[no_mangle]
pub extern "C" fn scheduler_get_current_pid(sched: *const Scheduler) -> usize {
    if sched.is_null() {
        return 0;
    }
    let scheduler = unsafe { &*sched };
    scheduler.current_pid.unwrap_or(0)
}

// =============================================================================
// Phase 6: User Process & Sandbox FFI
// =============================================================================

#[no_mangle]
pub extern "C" fn scheduler_create_user_process(
    sched: *mut Scheduler,
    priority: u8,
    command: *const c_char,
) -> usize {
    if sched.is_null() || command.is_null() {
        return 0;
    }
    let sched = unsafe { &mut *sched };
    
    let command_str = unsafe {
        let c_str = core::ffi::CStr::from_ptr(command);
        match c_str.to_str() {
            Ok(s) => s.to_string(),
            Err(_) => String::from("unknown"),
        }
    };
    
    let pid = sched.create_process(priority, command_str);
    
    // Set user process attributes
    if let Some(proc) = sched.processes.get_mut(&pid) {
        proc.process_type = ProcessType::User;
        proc.privilege_ring = PrivilegeRing::Ring3;
        proc.capabilities = 0x0000000000001B26u64; // CAP_SERIAL_WRITE | CAP_ALLOC_MEMORY | CAP_IPC_SEND | CAP_IPC_RECEIVE
        proc.user_memory_base = 0x1000000;  // 16MB user space start
        proc.user_memory_size = 4 * 1024 * 1024; // 4MB per user process
    }
    
    pid
}

#[no_mangle]
pub extern "C" fn scheduler_create_system_process(
    sched: *mut Scheduler,
    priority: u8,
    command: *const c_char,
) -> usize {
    if sched.is_null() || command.is_null() {
        return 0;
    }
    let sched = unsafe { &mut *sched };
    
    let command_str = unsafe {
        let c_str = core::ffi::CStr::from_ptr(command);
        match c_str.to_str() {
            Ok(s) => s.to_string(),
            Err(_) => String::from("unknown"),
        }
    };
    
    let pid = sched.create_process(priority, command_str);
    
    // Set system process attributes
    if let Some(proc) = sched.processes.get_mut(&pid) {
        proc.process_type = ProcessType::System;
        proc.privilege_ring = PrivilegeRing::Ring2;
        proc.capabilities = 0x0000000FFFFFu64; // Most capabilities
        proc.user_memory_base = 0;
        proc.user_memory_size = 0; // System procs not sandboxed
    }
    
    pid
}

#[no_mangle]
pub extern "C" fn scheduler_get_process_privilege(
    sched: *const Scheduler,
    pid: usize,
) -> u32 {
    if sched.is_null() {
        return 0;
    }
    let sched = unsafe { &*sched };
    
    if let Some(proc) = sched.processes.get(&pid) {
        proc.privilege_ring as u32
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn scheduler_get_process_type(
    sched: *const Scheduler,
    pid: usize,
) -> u32 {
    if sched.is_null() {
        return 0;
    }
    let sched = unsafe { &*sched };
    
    if let Some(proc) = sched.processes.get(&pid) {
        proc.process_type as u32
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn scheduler_grant_capability(
    sched: *mut Scheduler,
    pid: usize,
    cap: u64,
) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &mut *sched };
    
    if let Some(proc) = sched.processes.get_mut(&pid) {
        proc.capabilities |= cap;
        true
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn scheduler_revoke_capability(
    sched: *mut Scheduler,
    pid: usize,
    cap: u64,
) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &mut *sched };
    
    if let Some(proc) = sched.processes.get_mut(&pid) {
        proc.capabilities &= !cap;
        true
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn scheduler_has_capability(
    sched: *const Scheduler,
    pid: usize,
    cap: u64,
) -> bool {
    if sched.is_null() {
        return false;
    }
    let sched = unsafe { &*sched };
    
    if let Some(proc) = sched.processes.get(&pid) {
        (proc.capabilities & cap) == cap
    } else {
        false
    }
}

#[no_mangle]
pub extern "C" fn scheduler_get_user_memory_layout(
    sched: *const Scheduler,
    pid: usize,
    out_base: *mut u32,
    out_size: *mut u32,
) -> bool {
    if sched.is_null() || out_base.is_null() || out_size.is_null() {
        return false;
    }
    let sched = unsafe { &*sched };
    
    if let Some(proc) = sched.processes.get(&pid) {
        unsafe {
            ptr::write(out_base, proc.user_memory_base as u32);
            ptr::write(out_size, proc.user_memory_size as u32);
        }
        true
    } else {
        false
    }
}