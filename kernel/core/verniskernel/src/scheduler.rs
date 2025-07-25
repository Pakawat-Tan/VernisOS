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

// Dummy Instant struct for no_std
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Instant(u64);
impl Instant {
    pub fn now() -> Self { Instant(0) }
    pub fn duration_since(&self, earlier: Instant) -> Duration { Duration::from_secs(self.0.saturating_sub(earlier.0)) }
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
        }
    }

    pub fn get_effective_priority(&self) -> u8 {
        // Calculate effective priority based on nice value and aging
        let base_priority = self.priority as i16;
        let nice_adjustment = (self.nice as i16) * 2; // Nice affects priority
        let effective = base_priority + nice_adjustment;
        
        // Clamp to valid range (0-139)
        effective.max(0).min(139) as u8
    }

    pub fn update_cpu_time(&mut self, duration: Duration) {
        self.cpu_time += duration;
    }

    pub fn reset_time_slice(&mut self) {
        self.time_slice_remaining = self.time_quantum;
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
            return true;
        }
        false
    }

    pub fn set_nice(&mut self, pid: usize, nice: i8) -> bool {
        if let Some(proc) = self.processes.get_mut(&pid) {
            proc.nice = nice.max(-20).min(19); // Clamp to valid range
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