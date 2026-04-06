#ifndef VERNISOS_SANDBOX_H
#define VERNISOS_SANDBOX_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// User Sandbox & Security Model
// Phase 6: Privilege Isolation + Memory Sandboxing
// =============================================================================

// ---- Privilege Rings ----
typedef enum {
    PRIV_RING_KERNEL = 0,   // Ring 0 - kernel-mode code
    PRIV_RING_DRIVER = 1,   // Ring 1 - device drivers (optional)
    PRIV_RING_SYSTEM = 2,   // Ring 2 - system services (optional)
    PRIV_RING_USER   = 3    // Ring 3 - user-mode code
} PrivilegeRing;

// ---- Process Types ----
typedef enum {
    PROC_TYPE_KERNEL,    // Kernel process (pid=0, system tasks)
    PROC_TYPE_SYSTEM,    // System services (filesystem, network, etc.)
    PROC_TYPE_USER       // User application
} ProcessType;

// ---- Capabilities (Bitmask) ----
// Each bit represents a capability the process can use
typedef uint64_t Capability;

#define CAP_NONE                0x0000000000000000ULL

// I/O Capabilities
#define CAP_SERIAL_WRITE        0x0000000000000001ULL  // Write to serial port
#define CAP_SERIAL_READ         0x0000000000000002ULL  // Read from serial port
#define CAP_VGA_WRITE           0x0000000000000004ULL  // Write to VGA memory
#define CAP_IO_PORT             0x0000000000000008ULL  // Direct port I/O

// Memory Capabilities
#define CAP_ALLOC_MEMORY        0x0000000000000010ULL  // Allocate heap memory
#define CAP_MAP_MEMORY          0x0000000000000020ULL  // Map physical memory
#define CAP_PROTECT_MEMORY      0x0000000000000040ULL  // Change page protection

// Process Management
#define CAP_CREATE_PROCESS      0x0000000000000100ULL  // Create child process
#define CAP_KILL_PROCESS        0x0000000000000200ULL  // Terminate other process
#define CAP_CHANGE_PRIORITY     0x0000000000000400ULL  // Change scheduling priority

// IPC Capabilities
#define CAP_IPC_SEND            0x0000000000001000ULL  // Send IPC messages
#define CAP_IPC_RECEIVE         0x0000000000002000ULL  // Receive IPC messages
#define CAP_CHANNEL_CREATE      0x0000000000004000ULL  // Create IPC channels

// Module Capabilities
#define CAP_MODULE_LOAD         0x0000000000010000ULL  // Load kernel modules
#define CAP_MODULE_UNLOAD       0x0000000000020000ULL  // Unload kernel modules
#define CAP_MODULE_EXECUTE      0x0000000000040000ULL  // Execute module functions

// System Admin
#define CAP_SYS_DEBUG           0x0000000000100000ULL  // Debug system state
#define CAP_SYS_REBOOT          0x0000000000200000ULL  // Reboot system
#define CAP_SYS_TIME            0x0000000000400000ULL  // Set system time

// Predefined capability sets
#define CAP_KERNEL_ALL          0xFFFFFFFFFFFFFFFFULL  // All capabilities

#define CAP_SYSTEM_DEFAULT      (CAP_SERIAL_WRITE | CAP_SERIAL_READ | \
                                 CAP_ALLOC_MEMORY | CAP_MAP_MEMORY | \
                                 CAP_IPC_SEND | CAP_IPC_RECEIVE | \
                                 CAP_SYS_DEBUG)

#define CAP_USER_DEFAULT        (CAP_SERIAL_WRITE | CAP_ALLOC_MEMORY | \
                                 CAP_IPC_SEND | CAP_IPC_RECEIVE)

// ---- Memory Regions ----
// Kernel space: [0x0, 0x100000)
// User space base: 0x100000
// Each user process gets isolated heap/stack

typedef struct {
    uint32_t user_base;        // User space start address (typically 0x100000)
    uint32_t user_size;        // User space size in bytes
    uint32_t stack_base;       // Stack base (usually at top of user space)
    uint32_t stack_size;       // Stack size (typically 64KB)
    uint32_t heap_base;        // Heap base (after code section)
    uint32_t heap_size;        // Current/max heap size
} UserMemoryLayout;

// ---- Process Security Context ----
typedef struct {
    uint32_t       pid;                // Process ID
    ProcessType    proc_type;          // Kernel, System, or User
    PrivilegeRing  runtime_ring;       // Actual execution privilege
    Capability     capabilities;       // Bitmask of allowed operations
    
    // Memory isolation
    UserMemoryLayout mem_layout;       // Virtual address space for user procs
    uint32_t       cr3_value;          // Page directory (for future paging)
    
    // Resource limits
    uint32_t       max_memory;         // Maximum heap size (bytes)
    uint32_t       max_open_files;    // Maximum open file descriptors
    uint32_t       max_cpu_time;      // Maximum CPU time slice (ms)
    
    // Audit/tracking
    uint64_t       created_at;         // Creation timestamp
    uint64_t       syscalls_made;      // Count of syscalls issued
    uint64_t       capability_denials; // Count of blocked operations
} SecurityContext;

// ---- Syscall Filtering ----
// Returns true if process is allowed to make this syscall
bool sandbox_check_syscall(const SecurityContext *ctx, uint32_t syscall_num);

// ---- Initialization ----
void sandbox_init(void);
SecurityContext* sandbox_create_process(ProcessType type, uint8_t priority);
void sandbox_destroy_process(SecurityContext *ctx);

// ---- Capability Management ----
void sandbox_grant_capability(SecurityContext *ctx, Capability cap);
void sandbox_revoke_capability(SecurityContext *ctx, Capability cap);
bool sandbox_has_capability(const SecurityContext *ctx, Capability cap);

// ---- Memory Management ----
void sandbox_setup_user_memory(SecurityContext *ctx, uint32_t base, uint32_t size);
bool sandbox_validate_user_pointer(const SecurityContext *ctx, const void *ptr, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif // VERNISOS_SANDBOX_H
