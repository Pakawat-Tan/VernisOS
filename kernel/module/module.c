// module.c — Kernel Module Loader
// Phase 5: Module Loader + Dynamic Linking
//
// Architecture:
//   C side  : validates headers, manages slot table, executes module functions
//   Rust side: ModuleRegistry (BTreeMap) tracks metadata for queries
//
// Modules are flat binaries already present in kernel memory.
// No filesystem access yet — modules embedded in BSS or passed by address.

#include "module.h"
#include "dylib.h"
#include "ai_bridge.h"
#include <stddef.h>

// Forward declarations of kernel helpers (defined in kernel_x86.c / kernel_x64.c)
extern void serial_print(const char *s);
extern void serial_print_hex(uint32_t val);
extern void serial_print_dec(uint32_t val);

// Rust ModuleRegistry FFI
extern void    *module_registry_new(void);
extern int32_t  module_registry_register(void *reg, uint32_t mid,
                                          const char *name, uint32_t base_addr,
                                          uint32_t code_size, uint32_t fn_count);
extern int32_t  module_registry_unregister(void *reg, uint32_t mid);
extern uint32_t module_registry_count(void *reg);

// =============================================================================
// Internal slot table
// =============================================================================

typedef struct {
    uint8_t  in_use;
    uint32_t mid;
    char     name[MOD_NAME_LEN];
    uint32_t base_addr;              // address of ModHeader (start of binary)
    uint32_t code_size;
    uint32_t fn_count;
    uint32_t fn_offsets[MOD_MAX_EXPORTS];
} ModSlot;

static ModSlot   g_slots[MOD_MAX_MODULES];
static void     *g_registry = NULL;   // Rust ModuleRegistry*

// =============================================================================
// Helpers
// =============================================================================

static void mod_memset(void *dst, uint8_t val, uint32_t n) {
    uint8_t *p = (uint8_t *)dst;
    while (n--) *p++ = val;
}

static void mod_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static uint32_t mod_strlen(const char *s) {
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

// =============================================================================
// Init
// =============================================================================

void module_init(void) {
    mod_memset(g_slots, 0, sizeof(g_slots));
    g_registry = module_registry_new();
    dylib_init();
    serial_print("[module] initialized: ");
    serial_print_dec(MOD_MAX_MODULES);
    serial_print(" slots, registry=");
    serial_print_hex((uint32_t)(uintptr_t)g_registry);
    serial_print("\n");
}

// =============================================================================
// module_load
// =============================================================================

int32_t module_load(uint32_t addr, uint32_t size) {
    if (!addr || size < sizeof(ModHeader))
        return MOD_ERR_INVAL;

    const ModHeader *hdr = (const ModHeader *)(uintptr_t)addr;

    // Validate magic + version
    if (hdr->magic != MOD_MAGIC) {
        serial_print("[module] load: bad magic\n");
        return MOD_ERR_MAGIC;
    }
    if (hdr->version != MOD_VERSION) {
        serial_print("[module] load: unsupported version\n");
        return MOD_ERR_MAGIC;
    }
    if (hdr->fn_count > MOD_MAX_EXPORTS) {
        serial_print("[module] load: fn_count exceeds limit\n");
        return MOD_ERR_INVAL;
    }
    if (size < sizeof(ModHeader) + hdr->code_size) {
        serial_print("[module] load: size too small for code\n");
        return MOD_ERR_INVAL;
    }

    // Find free slot
    int32_t slot_idx = -1;
    for (int i = 0; i < MOD_MAX_MODULES; i++) {
        if (!g_slots[i].in_use) { slot_idx = i; break; }
    }
    if (slot_idx < 0) {
        serial_print("[module] load: no free slot\n");
        return MOD_ERR_NOSLOT;
    }

    ModSlot *slot = &g_slots[slot_idx];
    mod_memset(slot, 0, sizeof(ModSlot));
    slot->in_use    = 1;
    slot->mid       = (uint32_t)slot_idx;
    slot->base_addr = addr;
    slot->code_size = hdr->code_size;
    slot->fn_count  = hdr->fn_count;

    // Copy name (clamp to MOD_NAME_LEN-1)
    uint32_t name_len = mod_strlen(hdr->name);
    if (name_len >= MOD_NAME_LEN) name_len = MOD_NAME_LEN - 1;
    mod_memcpy(slot->name, hdr->name, name_len);
    slot->name[name_len] = '\0';

    // Copy fn_offsets
    for (uint32_t i = 0; i < hdr->fn_count; i++)
        slot->fn_offsets[i] = hdr->fn_offsets[i];

    // Notify Rust registry
    if (g_registry)
        module_registry_register(g_registry, slot->mid, slot->name,
                                 slot->base_addr, slot->code_size, slot->fn_count);

    serial_print("[module] loaded '");
    serial_print(slot->name);
    serial_print("' mid=");
    serial_print_dec(slot->mid);
    serial_print(" fn_count=");
    serial_print_dec(slot->fn_count);
    serial_print(" base=");
    serial_print_hex(slot->base_addr);
    serial_print("\n");

    // Phase 10: Notify AI of module load
    {
        char evbuf[64];
        ai_build_event(evbuf, sizeof(evbuf), slot->name, "load", (void*)0);
        ai_send_event(AI_EVT_MODULE, evbuf);
    }

    return (int32_t)slot->mid;
}

// =============================================================================
// module_unload
// =============================================================================

int32_t module_unload(uint32_t mid) {
    if (mid >= MOD_MAX_MODULES) return MOD_ERR_INVAL;
    ModSlot *slot = &g_slots[mid];
    if (!slot->in_use) return MOD_ERR_NOTFOUND;

    serial_print("[module] unloaded '");
    serial_print(slot->name);
    serial_print("' mid=");
    serial_print_dec(mid);
    serial_print("\n");

    // Phase 10: Notify AI of module unload
    {
        char evbuf[64];
        ai_build_event(evbuf, sizeof(evbuf), slot->name, "unload", (void*)0);
        ai_send_event(AI_EVT_MODULE, evbuf);
    }

    if (g_registry)
        module_registry_unregister(g_registry, mid);

    mod_memset(slot, 0, sizeof(ModSlot));
    return MOD_OK;
}

// =============================================================================
// module_list
// =============================================================================

int32_t module_list(void) {
    int32_t count = 0;
    serial_print("[module] loaded modules:\n");
    for (int i = 0; i < MOD_MAX_MODULES; i++) {
        if (!g_slots[i].in_use) continue;
        serial_print("  mid=");
        serial_print_dec((uint32_t)i);
        serial_print(" '");
        serial_print(g_slots[i].name);
        serial_print("' fn=");
        serial_print_dec(g_slots[i].fn_count);
        serial_print(" base=");
        serial_print_hex(g_slots[i].base_addr);
        serial_print("\n");
        count++;
    }
    if (!count) serial_print("  (none)\n");
    return count;
}

// =============================================================================
// module_call
// =============================================================================

int32_t module_call(uint32_t mid, uint32_t fn_id, uint32_t arg) {
    if (mid >= MOD_MAX_MODULES) return MOD_ERR_INVAL;
    const ModSlot *slot = &g_slots[mid];
    if (!slot->in_use)          return MOD_ERR_NOTFOUND;
    if (fn_id >= slot->fn_count) return MOD_ERR_RANGE;

    // Code section starts right after the ModHeader
    uint32_t code_base = slot->base_addr + (uint32_t)sizeof(ModHeader);
    uint32_t fn_addr   = code_base + slot->fn_offsets[fn_id];

    ModFn fn = (ModFn)(uintptr_t)fn_addr;
    return (int32_t)fn(arg);
}

// =============================================================================
// module_info
// =============================================================================

int32_t module_info(uint32_t mid, ModInfo *out) {
    if (!out || mid >= MOD_MAX_MODULES) return MOD_ERR_INVAL;
    const ModSlot *slot = &g_slots[mid];
    if (!slot->in_use) return MOD_ERR_NOTFOUND;

    out->mid       = slot->mid;
    out->base_addr = slot->base_addr;
    out->code_size = slot->code_size;
    out->fn_count  = slot->fn_count;
    mod_memcpy(out->name, slot->name, MOD_NAME_LEN);
    return MOD_OK;
}

// =============================================================================
// Syscall Dispatcher
// Syscall convention (x86 INT 0x80 / x64 SYSCALL): num, a1, a2, a3
// =============================================================================

int32_t module_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3) {
    switch (num) {

    // SYS_MOD_LOAD (28): a1=addr, a2=size → mid
    case SYS_MOD_LOAD:
        return module_load(a1, a2);

    // SYS_MOD_UNLOAD (29): a1=mid
    case SYS_MOD_UNLOAD:
        return module_unload(a1);

    // SYS_MOD_LIST (30): → count
    case SYS_MOD_LIST:
        return module_list();

    // SYS_MOD_CALL (31): a1=mid, a2=fn_id, a3=arg
    case SYS_MOD_CALL:
        return module_call(a1, a2, a3);

    // SYS_MOD_INFO (32): a1=mid, a2=ptr to ModInfo
    case SYS_MOD_INFO:
        if (!a2) return MOD_ERR_INVAL;
        return module_info(a1, (ModInfo *)(uintptr_t)a2);

    default:
        return MOD_ERR_INVAL;
    }
}