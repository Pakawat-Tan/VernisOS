// syscall.rs

// use alloc::string::String;
use alloc::vec::Vec;
use core::option::Option::{Some, None};
use core::result::Result::Ok;
use alloc::format;

// Kernel structures
pub struct ProcessContext {
    pub pid: u32,
    pub registers: [u32; 16], // Simplified register set
    pub status: ProcessStatus,
}

#[derive(Debug)]
pub enum ProcessStatus {
    Running,
    Ready,
    Blocked,
    Zombie,
}

pub struct Scheduler {
    pub current_pid: Option<u32>,
    pub ready_queue: Vec<u32>,
}

// Global kernel state (ในระบบจริงจะอยู่ใน kernel space)
static mut CURRENT_PROCESS: Option<ProcessContext> = None;
static mut SCHEDULER: Scheduler = Scheduler {
    current_pid: None,
    ready_queue: Vec::new(),
};

// Console driver interface
// คอมเมนต์หรือเอาออก extern "C" fn console_write

#[derive(Debug)]
pub enum SyscallNumber {
    Write = 1,
    Exit = 2,
    GetPid = 3,
    DumpRegisters = 10,
    DumpScheduler = 11,
    DumpMemory = 12,
    DumpSyscalls = 13,
    DumpAll = 14,
}

impl SyscallNumber {
    pub fn from_u32(num: u32) -> Option<Self> {
        match num {
            1 => Some(SyscallNumber::Write),
            2 => Some(SyscallNumber::Exit),
            3 => Some(SyscallNumber::GetPid),
            10 => Some(SyscallNumber::DumpRegisters),
            11 => Some(SyscallNumber::DumpScheduler),
            12 => Some(SyscallNumber::DumpMemory),
            13 => Some(SyscallNumber::DumpSyscalls),
            14 => Some(SyscallNumber::DumpAll),
            _ => None,
        }
    }
}

#[no_mangle]
pub extern "C" fn syscall_handler(sys_num: u32, arg1: usize, arg2: usize, _arg3: usize) -> isize {
    match SyscallNumber::from_u32(sys_num) {
        Some(SyscallNumber::Write) => sys_write(arg1 as *const u8, arg2),
        Some(SyscallNumber::Exit) => {
            sys_exit(arg1 as i32);
            0
        }
        Some(SyscallNumber::GetPid) => sys_getpid(),
        Some(SyscallNumber::DumpRegisters) => {
            sys_dump_registers();
            0
        }
        Some(SyscallNumber::DumpScheduler) => {
            sys_dump_scheduler();
            0
        }
        Some(SyscallNumber::DumpMemory) => {
            sys_dump_memory(arg1);
            0
        }
        Some(SyscallNumber::DumpSyscalls) => {
            sys_dump_syscalls();
            0
        }
        Some(SyscallNumber::DumpAll) => {
            sys_dump_all();
            0
        }
        None => {
            // Invalid syscall number
            -1
        }
    }
}

// ===========================
// System Call Implementations
// ===========================

pub fn sys_write(ptr: *const u8, len: usize) -> isize {
    unsafe {
        if !is_valid_user_memory(ptr, len) {
            return -1; // EFAULT
        }

        let slice = core::slice::from_raw_parts(ptr, len);
        if let Ok(msg) = core::str::from_utf8(slice) {
            kernel_print(msg);
            msg.len() as isize
        } else {
            -2 // EINVAL: invalid utf-8
        }
    }
}

pub fn sys_exit(code: i32) {
    unsafe {
        if let Some(ref mut process) = CURRENT_PROCESS {
            process.status = ProcessStatus::Zombie;
            kernel_print(&format!("[EXIT] Process {} exited with code {}\n", process.pid, code));
        }
        schedule_next();
    }
}

pub fn sys_getpid() -> isize {
    unsafe {
        match CURRENT_PROCESS {
            Some(ref process) => process.pid as isize,
            None => -1,
        }
    }
}

pub fn sys_dump_registers() {
    unsafe {
        if let Some(ref process) = CURRENT_PROCESS {
            kernel_print(&format!("[DUMP] Registers for PID {}:\n", process.pid));
            for (i, reg) in process.registers.iter().enumerate() {
                kernel_print(&format!("  R{}: 0x{:08X}\n", i, reg));
            }
        } else {
            kernel_print("[ERROR] No current process\n");
        }
    }
}

pub fn sys_dump_scheduler() {
    unsafe {
        let sched_ptr = core::ptr::addr_of!(SCHEDULER);
        kernel_print("[DUMP] Scheduler State:\n");
        let pid = (*sched_ptr).current_pid;
        kernel_print(&format!("  Current PID: {:?}\n", pid));
        let len = (*sched_ptr).ready_queue.len();
        kernel_print(&format!("  Ready Queue Length: {}\n", len));
        kernel_print("  Ready Queue PIDs: [");
        let queue = &(*sched_ptr).ready_queue;
        for (i, pid) in queue.iter().enumerate() {
            if i > 0 { kernel_print(", "); }
            kernel_print(&format!("{}", pid));
        }
        kernel_print("]\n");
    }
}

pub fn sys_dump_memory(addr: usize) {
    unsafe {
        if !is_valid_user_memory(addr as *const u8, 1) {
            kernel_print("[ERROR] Invalid memory address\n");
            return;
        }

        let value = core::ptr::read_volatile(addr as *const u8);
        kernel_print(&format!("[DUMP] Memory at 0x{:X} => 0x{:02X}\n", addr, value));
    }
}

pub fn sys_dump_syscalls() {
    kernel_print("[DUMP] Available System Calls:\n");
    kernel_print("  1  - Write:         Write string to console\n");
    kernel_print("  2  - Exit:          Exit process with code\n");
    kernel_print("  3  - GetPid:        Get current process ID\n");
    kernel_print("  10 - DumpRegisters: Dump current process registers\n");
    kernel_print("  11 - DumpScheduler: Dump scheduler state\n");
    kernel_print("  12 - DumpMemory:    Dump memory at address\n");
    kernel_print("  13 - DumpSyscalls:  Show this help\n");
    kernel_print("  14 - DumpAll:       Dump all system information\n");
}

pub fn sys_dump_all() {
    kernel_print("=== VERNISOS SYSTEM DUMP ===\n");

    // Current process info
    unsafe {
        if let Some(ref process) = CURRENT_PROCESS {
            kernel_print(&format!("Current Process: PID={}, Status={:?}\n",
                process.pid, process.status));
        } else {
            kernel_print("No current process\n");
        }
    }

    // Scheduler state
    sys_dump_scheduler();
    
    // Process registers
    sys_dump_registers();
    
    // Available syscalls
    sys_dump_syscalls();

    kernel_print("=== END SYSTEM DUMP ===\n");
}

// ===========================
// Helper Functions
// ===========================

fn is_valid_user_memory(ptr: *const u8, len: usize) -> bool {
    // Basic validation - ในระบบจริงจะต้องตรวจสอบ memory mapping
    if ptr.is_null() || len == 0 {
        return false;
    }
    
    // ตรวจสอบว่าไม่เกิน address space limit
    let end_addr = ptr as usize + len;
    if end_addr < ptr as usize {
        return false; // Integer overflow
    }
    
    // ในระบบจริงจะตรวจสอบกับ page table และ memory permissions
    true
}

fn kernel_print(_msg: &str) {
    // TODO: implement kernel log/serial output here
}

fn schedule_next() {
    unsafe {
        let mut_sched_ptr = core::ptr::addr_of_mut!(SCHEDULER);
        if let Some(current) = (*mut_sched_ptr).current_pid {
            if let Some(ref process) = CURRENT_PROCESS {
                if !matches!(process.status, ProcessStatus::Zombie) {
                    (*mut_sched_ptr).ready_queue.push(current);
                }
            }
        }
        if let Some(next_pid) = (*mut_sched_ptr).ready_queue.pop() {
            (*mut_sched_ptr).current_pid = Some(next_pid);
            kernel_print(&format!("[SCHED] Switching to PID {}\n", next_pid));
        } else {
            (*mut_sched_ptr).current_pid = None;
            kernel_print("[SCHED] No ready processes\n");
        }
    }
}

// ===========================
// Initialization
// ===========================

pub fn init() {
    unsafe {
        let mut_sched_ptr = core::ptr::addr_of_mut!(SCHEDULER);
        (*mut_sched_ptr).current_pid = Some(1);
        (*mut_sched_ptr).ready_queue.clear();
        (*mut_sched_ptr).ready_queue.push(2);
        (*mut_sched_ptr).ready_queue.push(3);
        CURRENT_PROCESS = Some(ProcessContext {
            pid: 1,
            registers: [0; 16],
            status: ProcessStatus::Running,
        });
        kernel_print("[INIT] Syscall handler initialized\n");
        kernel_print("[INIT] Current PID: 1, Ready Queue: [2, 3]\n");
    }
}

#[no_mangle]
pub extern "C" fn ffi_sys_write(ptr: *const u8, len: usize) -> isize {
    sys_write(ptr, len)
}

#[no_mangle]
pub extern "C" fn ffi_sys_exit(code: i32) {
    sys_exit(code)
}

#[no_mangle]
pub extern "C" fn ffi_sys_getpid() -> isize {
    sys_getpid()
}

#[no_mangle]
pub extern "C" fn ffi_sys_dump_registers() {
    sys_dump_registers()
}

#[no_mangle]
pub extern "C" fn ffi_sys_dump_scheduler() {
    sys_dump_scheduler()
}

#[no_mangle]
pub extern "C" fn ffi_sys_dump_memory(addr: usize) {
    sys_dump_memory(addr)
}

#[no_mangle]
pub extern "C" fn ffi_sys_dump_syscalls() {
    sys_dump_syscalls()
}

#[no_mangle]
pub extern "C" fn ffi_sys_dump_all() {
    sys_dump_all()
}

#[no_mangle]
pub extern "C" fn ffi_test_simple() {}

#[no_mangle]
pub extern "C" fn syscall_init() {}