// sandbox.c — User Process Sandboxing
// Phase 6: Privilege Isolation + Memory Sandboxing

#include "sandbox.h"
#include "ai_bridge.h"
#include <stddef.h>

// Forward declarations  
extern void serial_print(const char *s);
extern void serial_print_hex(uint32_t val);
extern void serial_print_dec(uint32_t val);

// =============================================================================
// Syscall Whitelists by Process Type — Bitmask optimization (Phase 15)
// Bit N = syscall N is allowed. O(1) lookup instead of O(N) linear scan.
// =============================================================================

// Kernel: all syscalls 0-32 allowed
static const uint64_t KERNEL_ALLOWED_MASK = 0x1FFFFFFFFULL; // bits 0-32

// System services: 0,1,2, 20-27
static const uint64_t SYSTEM_ALLOWED_MASK =
    (1ULL << 0) | (1ULL << 1) | (1ULL << 2) |
    (1ULL << 20) | (1ULL << 21) | (1ULL << 22) | (1ULL << 23) |
    (1ULL << 24) | (1ULL << 25) | (1ULL << 26) | (1ULL << 27);

// User processes: 0,1,2, 20-22
static const uint64_t USER_ALLOWED_MASK =
    (1ULL << 0) | (1ULL << 1) | (1ULL << 2) |
    (1ULL << 20) | (1ULL << 21) | (1ULL << 22);

static inline bool syscall_allowed_by_mask(uint32_t syscall_num, uint64_t mask) {
    if (syscall_num >= 64) return false;
    return (mask >> syscall_num) & 1;
}

// =============================================================================
// Helper: notify AI of denied syscall
// =============================================================================

static void sandbox_notify_deny(uint32_t pid, uint32_t syscall_num) {
    char evbuf[48];
    char pidbuf[12]; char sysbuf[12];
    int i = 0; char tmp[12]; uint32_t v = pid;
    if (v == 0) { tmp[i++] = '0'; } else { while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
    int p = 0; for (int j = i - 1; j >= 0; j--) pidbuf[p++] = tmp[j]; pidbuf[p] = '\0';
    i = 0; v = syscall_num;
    if (v == 0) { tmp[i++] = '0'; } else { while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; } }
    p = 0; for (int j = i - 1; j >= 0; j--) sysbuf[p++] = tmp[j]; sysbuf[p] = '\0';
    ai_build_event(evbuf, sizeof(evbuf), pidbuf, sysbuf, (void*)0);
    ai_send_event(AI_EVT_DENY, evbuf);
}

// =============================================================================
// Syscall Filtering
// =============================================================================

bool sandbox_check_syscall(const SecurityContext *ctx, uint32_t syscall_num) {
    if (!ctx) return false;

    // Kernel processes can do anything
    if (ctx->proc_type == PROC_TYPE_KERNEL)
        return true;

    // Check capability-based permissions first
    switch (syscall_num) {
        case 20: // SYS_IPC_REGISTER
        case 21: // SYS_IPC_SEND
        case 22: // SYS_IPC_RECV
        case 26: // SYS_IPC_CHANNEL_WRITE
        case 27: // SYS_IPC_CHANNEL_READ
            if (!sandbox_has_capability(ctx, CAP_IPC_SEND | CAP_IPC_RECEIVE)) {
                sandbox_notify_deny(ctx->pid, syscall_num);
                return false;
            }
            break;

        case 28: // SYS_MOD_LOAD
        case 29: // SYS_MOD_UNLOAD
            if (!sandbox_has_capability(ctx, CAP_MODULE_LOAD | CAP_MODULE_UNLOAD)) {
                sandbox_notify_deny(ctx->pid, syscall_num);
                return false;
            }
            break;

        case 31: // SYS_MOD_CALL
            if (!sandbox_has_capability(ctx, CAP_MODULE_EXECUTE)) {
                sandbox_notify_deny(ctx->pid, syscall_num);
                return false;
            }
            break;
    }

    // Check against bitmask whitelist based on process type (O(1))
    if (ctx->proc_type == PROC_TYPE_SYSTEM) {
        bool ok = syscall_allowed_by_mask(syscall_num, SYSTEM_ALLOWED_MASK);
        if (!ok) sandbox_notify_deny(ctx->pid, syscall_num);
        return ok;
    } else if (ctx->proc_type == PROC_TYPE_USER) {
        bool ok = syscall_allowed_by_mask(syscall_num, USER_ALLOWED_MASK);
        if (!ok) sandbox_notify_deny(ctx->pid, syscall_num);
        return ok;
    }

    sandbox_notify_deny(ctx->pid, syscall_num);
    return false;
}

// =============================================================================
// Initialization
// =============================================================================

void sandbox_init(void) {
    serial_print("[sandbox] initialized\n");
}

// =============================================================================
// Process Creation
// =============================================================================

SecurityContext* sandbox_create_process(ProcessType type, uint8_t priority) {
    // In phase 6, we allocate from static memory pool or heap
    // For now, return a stub that integrates with scheduler
    static SecurityContext pool[16];
    static size_t pool_idx = 0;

    if (pool_idx >= 16) {
        serial_print("[sandbox] ERROR: process pool exhausted\n");
        return NULL;
    }

    SecurityContext *ctx = &pool[pool_idx++];
    ctx->pid = pool_idx - 1;
    ctx->proc_type = type;
    ctx->capability_denials = 0;
    ctx->syscalls_made = 0;

    // Set default capabilities by type
    if (type == PROC_TYPE_KERNEL) {
        ctx->capabilities = CAP_KERNEL_ALL;
        ctx->runtime_ring = PRIV_RING_KERNEL;
    } else if (type == PROC_TYPE_SYSTEM) {
        ctx->capabilities = CAP_SYSTEM_DEFAULT;
        ctx->runtime_ring = PRIV_RING_SYSTEM;
    } else {
        ctx->capabilities = CAP_USER_DEFAULT;
        ctx->runtime_ring = PRIV_RING_USER;
    }

    // Set default resource limits
    ctx->max_memory = (type == PROC_TYPE_USER) ? 512 * 1024 : 2 * 1024 * 1024;  // 512KB for users, 2MB for system
    ctx->max_open_files = (type == PROC_TYPE_USER) ? 32 : 256;
    ctx->max_cpu_time = (type == PROC_TYPE_USER) ? 5000 : 0;  // 5 seconds for users, unlimited for system

    serial_print("[sandbox] created process type=");
    serial_print_dec(type);
    serial_print(" pid=");
    serial_print_dec(ctx->pid);
    serial_print(" ring=");
    serial_print_dec(ctx->runtime_ring);
    serial_print("\n");

    return ctx;
}

void sandbox_destroy_process(SecurityContext *ctx) {
    if (!ctx) return;
    serial_print("[sandbox] destroyed process pid=");
    serial_print_dec(ctx->pid);
    serial_print("\n");
}

// =============================================================================
// Capability Management
// =============================================================================

void sandbox_grant_capability(SecurityContext *ctx, Capability cap) {
    if (!ctx) return;
    ctx->capabilities |= cap;
}

void sandbox_revoke_capability(SecurityContext *ctx, Capability cap) {
    if (!ctx) return;
    ctx->capabilities &= ~cap;
}

bool sandbox_has_capability(const SecurityContext *ctx, Capability cap) {
    if (!ctx) return false;
    return (ctx->capabilities & cap) == cap;
}

// =============================================================================
// Memory Management
// =============================================================================

void sandbox_setup_user_memory(SecurityContext *ctx, uint32_t base, uint32_t size) {
    if (!ctx) return;

    ctx->mem_layout.user_base = base;
    ctx->mem_layout.user_size = size;
    ctx->mem_layout.stack_base = base + size - 4096;  // Stack at top
    ctx->mem_layout.stack_size = 4096;                 // 4KB stack
    ctx->mem_layout.heap_base = base;                  // Heap at bottom
    ctx->mem_layout.heap_size = ctx->mem_layout.stack_base - base;

    serial_print("[sandbox] user memory: base=");
    serial_print_hex(base);
    serial_print(" size=");
    serial_print_hex(size);
    serial_print(" stack=");
    serial_print_hex(ctx->mem_layout.stack_base);
    serial_print("\n");
}

// Validate that a pointer is within user's allowed memory region
bool sandbox_validate_user_pointer(const SecurityContext *ctx, const void *ptr, uint32_t size) {
    if (!ctx || ctx->proc_type == PROC_TYPE_KERNEL) {
        // Kernel can access anything
        return true;
    }

    uint32_t addr = (uint32_t)(uintptr_t)ptr;
    uint32_t end = addr + size;

    // Check if pointer is within user memory region
    if (addr < ctx->mem_layout.user_base || end > ctx->mem_layout.user_base + ctx->mem_layout.user_size) {
        // Cannot modify const ctx, just warn
        serial_print("[sandbox] WARNING: user pointer out of bounds: addr=");
        serial_print_hex(addr);
        serial_print("\n");
        return false;
    }

    return true;
}
