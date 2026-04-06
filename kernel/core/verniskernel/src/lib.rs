#![no_std]
#![feature(alloc_error_handler)]

extern crate alloc;

use core::panic::PanicInfo;
use buddy_system_allocator::LockedHeap;
// use alloc::string::ToString;

#[global_allocator]
static ALLOCATOR: LockedHeap<32> = LockedHeap::empty();

#[no_mangle]
pub extern "C" fn verniskernel_init_heap(heap_start: usize, heap_size: usize) {
    unsafe { ALLOCATOR.lock().init(heap_start, heap_size); }
}

#[alloc_error_handler]
fn alloc_error_handler(_layout: core::alloc::Layout) -> ! {
    loop {}
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

pub mod memory;
pub mod scheduler;
pub mod syscall;
pub mod module_registry;
pub mod ai;
pub mod font8x16;
pub mod framebuffer;
pub mod console;
pub mod mouse;
pub mod gui;

// Re-export FFI functions from module_registry to ensure they're linked
pub use module_registry::{
    module_registry_new,
    module_registry_register,
    module_registry_unregister,
    module_registry_count,
    module_registry_get_name,
};

// Re-export Phase 6 scheduler functions for user process/sandbox support
pub use scheduler::{
    scheduler_create_user_process,
    scheduler_create_system_process,
    scheduler_get_process_privilege,
    scheduler_get_process_type,
    scheduler_grant_capability,
    scheduler_revoke_capability,
    scheduler_has_capability,
    scheduler_get_user_memory_layout,
};

// Re-export ps/process-list FFI (Phase 7 CLI integration)
pub use scheduler::{PsRow, scheduler_get_pid_list, scheduler_get_ps_row};

// Re-export AI engine FFI (Phase 10 in-kernel AI)
pub use ai::{
    ai_engine_new,
    ai_engine_feed_event,
    ai_engine_tick,
    ai_engine_set_tune_cb,
    ai_engine_set_remediate_cb,
    ai_engine_event_count,
    ai_engine_anomaly_count,
    ai_engine_active_procs,
    ai_engine_decision_count,
    ai_engine_free,
};

// =============================================================================
// Print callback — registered from C so Rust modules can write to serial/VGA
// =============================================================================

static mut PRINT_CB: Option<unsafe extern "C" fn(*const u8, usize)> = None;

#[no_mangle]
pub unsafe extern "C" fn verniskernel_register_print(
    cb: unsafe extern "C" fn(*const u8, usize),
) {
    PRINT_CB = Some(cb);
}

/// Called by Rust modules to output a string through the C-registered callback.
pub fn kernel_print_raw(s: &str) {
    unsafe {
        if let Some(cb) = PRINT_CB {
            cb(s.as_ptr(), s.len());
        }
    }
}
