// module_registry.rs — Phase 5: Module Registry (Rust side)
//
// Mirrors the C slot table in a BTreeMap for structured queries.
// C (module.c) is the source of truth; Rust is notified via FFI on load/unload.

use alloc::collections::BTreeMap;
use alloc::string::{String, ToString};
use core::ffi::c_char;
use core::ptr;

// =============================================================================
// Data types
// =============================================================================

#[derive(Debug, Clone)]
pub struct ModuleEntry {
    pub mid:       u32,
    pub name:      String,
    pub base_addr: u32,
    pub code_size: u32,
    pub fn_count:  u32,
}

pub struct ModuleRegistry {
    entries: BTreeMap<u32, ModuleEntry>,
}

impl ModuleRegistry {
    pub fn new() -> Self {
        ModuleRegistry {
            entries: BTreeMap::new(),
        }
    }

    pub fn register(&mut self, mid: u32, name: String,
                    base_addr: u32, code_size: u32, fn_count: u32) -> bool {
        self.entries.insert(mid, ModuleEntry { mid, name, base_addr, code_size, fn_count });
        true
    }

    pub fn unregister(&mut self, mid: u32) -> bool {
        self.entries.remove(&mid).is_some()
    }

    pub fn get(&self, mid: u32) -> Option<&ModuleEntry> {
        self.entries.get(&mid)
    }

    pub fn count(&self) -> u32 {
        self.entries.len() as u32
    }
}

// =============================================================================
// FFI exports — called from module.c
// =============================================================================

#[no_mangle]
pub extern "C" fn module_registry_new() -> *mut ModuleRegistry {
    use alloc::boxed::Box;
    Box::into_raw(Box::new(ModuleRegistry::new()))
}

#[no_mangle]
pub extern "C" fn module_registry_register(
    reg:       *mut ModuleRegistry,
    mid:       u32,
    name_ptr:  *const c_char,
    base_addr: u32,
    code_size: u32,
    fn_count:  u32,
) -> i32 {
    if reg.is_null() || name_ptr.is_null() { return -1; }
    let reg = unsafe { &mut *reg };

    let name = unsafe {
        let c_str = core::ffi::CStr::from_ptr(name_ptr);
        match c_str.to_str() {
            Ok(s)  => s.to_string(),
            Err(_) => String::from("?"),
        }
    };

    reg.register(mid, name, base_addr, code_size, fn_count);
    0
}

#[no_mangle]
pub extern "C" fn module_registry_unregister(reg: *mut ModuleRegistry, mid: u32) -> i32 {
    if reg.is_null() { return -1; }
    let reg = unsafe { &mut *reg };
    if reg.unregister(mid) { 0 } else { -1 }
}

#[no_mangle]
pub extern "C" fn module_registry_count(reg: *const ModuleRegistry) -> u32 {
    if reg.is_null() { return 0; }
    let reg = unsafe { &*reg };
    reg.count()
}

// Fill name_buf (len bytes) with the name of module mid.
// Returns 0 on success, -1 if not found or buf too small.
#[no_mangle]
pub extern "C" fn module_registry_get_name(
    reg:      *const ModuleRegistry,
    mid:      u32,
    name_buf: *mut u8,
    buf_len:  u32,
) -> i32 {
    if reg.is_null() || name_buf.is_null() || buf_len == 0 { return -1; }
    let reg = unsafe { &*reg };

    match reg.get(mid) {
        None => -1,
        Some(entry) => {
            let bytes = entry.name.as_bytes();
            let copy_len = if bytes.len() < (buf_len as usize - 1) {
                bytes.len()
            } else {
                buf_len as usize - 1
            };
            unsafe {
                ptr::copy_nonoverlapping(bytes.as_ptr(), name_buf, copy_len);
                ptr::write(name_buf.add(copy_len), 0); // null-terminate
            }
            0
        }
    }
}