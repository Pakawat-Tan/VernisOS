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
