#ifndef VERNISOS_MODULE_H
#define VERNISOS_MODULE_H

#include <stdint.h>

// =============================================================================
// Module Loader — Phase 5: Module Loader + Dynamic Linking
//
// Module binary format (flat, no ELF):
//   [ModHeader 72 bytes][code/data bytes]
//
// All modules share kernel address space (no sandbox — that's Phase 6).
// Modules are "loaded" by registering an already-memory-resident binary
// (embedded in BSS or passed via address). No disk I/O yet (Phase 5).
// =============================================================================

// ---- Constants ----
#define MOD_MAGIC         0x4C444F4D   // "MODL" little-endian
#define MOD_VERSION       1
#define MOD_MAX_MODULES   8            // concurrent loaded modules
#define MOD_MAX_EXPORTS   8            // exported functions per module
#define MOD_NAME_LEN      24           // including null terminator

// ---- Return codes ----
#define MOD_OK             0
#define MOD_ERR_INVAL    (-1)          // bad argument / null pointer
#define MOD_ERR_NOSLOT   (-2)          // no free module slot
#define MOD_ERR_MAGIC    (-3)          // wrong magic or version
#define MOD_ERR_RANGE    (-4)          // fn_id out of range
#define MOD_ERR_NOTFOUND (-5)          // mid not found

// ---- Syscall numbers (follow IPC 20-27) ----
#define SYS_MOD_LOAD    28   // a1=addr, a2=size → mid (or error)
#define SYS_MOD_UNLOAD  29   // a1=mid → MOD_OK or error
#define SYS_MOD_LIST    30   // (no args) → count of loaded modules
#define SYS_MOD_CALL    31   // a1=mid, a2=fn_id, a3=arg → result
#define SYS_MOD_INFO    32   // a1=mid, a2=ptr to ModInfo → MOD_OK or error

// =============================================================================
// Module binary header — must be at start of module binary
// Total size: 4+4+4+4+24+32 = 72 bytes
// =============================================================================
typedef struct {
    uint32_t magic;                       // MOD_MAGIC
    uint32_t version;                     // MOD_VERSION
    uint32_t fn_count;                    // exported function count (≤ MOD_MAX_EXPORTS)
    uint32_t code_size;                   // bytes of code/data following this header
    char     name[MOD_NAME_LEN];         // null-terminated module name
    uint32_t fn_offsets[MOD_MAX_EXPORTS]; // byte offsets into code section per function
} __attribute__((packed)) ModHeader;      // 72 bytes total

// Module info — filled by SYS_MOD_INFO / module_info()
typedef struct {
    uint32_t mid;
    char     name[MOD_NAME_LEN];
    uint32_t base_addr;
    uint32_t code_size;
    uint32_t fn_count;
} ModInfo;

// Module function type: uint32_t fn(uint32_t arg)
typedef uint32_t (*ModFn)(uint32_t arg);

// =============================================================================
// Public API (C)
// =============================================================================

// Called once from kernel_main Phase 5 init block
void    module_init(void);

// Register a module binary already present at [addr, addr+size).
// Validates header, allocates a slot, notifies Rust registry.
// Returns mid (0-based slot index) or MOD_ERR_*.
int32_t module_load(uint32_t addr, uint32_t size);

// Unload module by mid. Returns MOD_OK or MOD_ERR_*.
int32_t module_unload(uint32_t mid);

// Print all loaded modules to serial. Returns count.
int32_t module_list(void);

// Call exported function fn_id of module mid with arg.
// Returns function's return value or MOD_ERR_*.
int32_t module_call(uint32_t mid, uint32_t fn_id, uint32_t arg);

// Fill *out with info about module mid. Returns MOD_OK or MOD_ERR_*.
int32_t module_info(uint32_t mid, ModInfo *out);

// Syscall dispatcher — called from c_syscall_handler for syscalls 28-32
int32_t module_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3);

#endif // VERNISOS_MODULE_H