#ifndef VERNISOS_AI_BRIDGE_H
#define VERNISOS_AI_BRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// AI IPC Bridge
// Phase 8: Kernel ↔ AI Engine Communication via COM2
//
// Architecture:
//   Kernel (C)  →  COM2 serial (0x2F8)  →  QEMU socket  →  Python ai_listener
//   Python resp →  QEMU socket           →  COM2          →  Kernel (C)
//
// Protocol (line-delimited text):
//   Kernel → Python:  "REQ|<session_id>|<payload>\n"
//   Kernel → Python:  "EVT|<event_type>|<data>\n"
//   Python → Kernel:  "RESP|<session_id>|<payload>\n"
//   Python → Kernel:  "CMD|<command>|<args>\n"
// =============================================================================

#define AI_COM_PORT         0x2F8       // COM2 base I/O port
#define AI_MSG_MAX          512         // max message payload length
#define AI_RESP_TIMEOUT     5000000UL   // spin-loop timeout (~5s at kernel speed)

// ---- Syscall numbers (Phase 8) ----
#define SYS_AI_QUERY        40          // send query, receive response
#define SYS_AI_STATUS       41          // get bridge status
#define SYS_AI_EVENT        42          // send event notification

// ---- Event types (string — backward compatible) ----
#define AI_EVT_BOOT         "BOOT"
#define AI_EVT_PROCESS      "PROC"
#define AI_EVT_MODULE       "MOD"
#define AI_EVT_EXCEPTION    "EXCP"
#define AI_EVT_DENY         "DENY"
#define AI_EVT_FAIL         "FAIL"
#define AI_EVT_SYSCALL_EVT  "SYSCALL"

// ---- Event type numeric codes (Phase 15 — O(1) dispatch) ----
#define AI_EVT_CODE_BOOT      1
#define AI_EVT_CODE_STAT      2
#define AI_EVT_CODE_EXCEPTION 3
#define AI_EVT_CODE_PROCESS   4
#define AI_EVT_CODE_MODULE    5
#define AI_EVT_CODE_DENY      6
#define AI_EVT_CODE_FAIL      7
#define AI_EVT_CODE_SYSCALL   8

// ---- Bridge status ----
typedef enum {
    AI_STATUS_OFFLINE   = 0,
    AI_STATUS_READY     = 1,
    AI_STATUS_BUSY      = 2,
    AI_STATUS_ERROR     = 3,
} AiBridgeStatus;

// =============================================================================
// Kernel-side API
// =============================================================================

// Initialize COM2 for AI communication
void ai_bridge_init(void);

// Check bridge status
AiBridgeStatus ai_bridge_status(void);

// Send a query and wait for response (synchronous, spin-poll)
// Returns response length, or -1 on timeout/error
int  ai_query_sync(const char *query, char *resp_buf, size_t max_len);

// Send event notification (fire-and-forget)
void ai_send_event(const char *event_type, const char *data);

// Poll for a CMD frame from the AI engine (non-blocking).
// Parses CMD|TUNE|action|target|value|reason and calls the registered handler.
// Call this from the keyboard idle loop for low-latency tuning.
void ai_poll_cmd(void);

// Tune-decision callback type.
// action : SCHED_QUANTUM | SCHED_PRIO | MEM_PRESSURE | THROTTLE | IDLE
// target : "scheduler" | "memory" | process name
// value  : numeric suggestion (quantum ms, priority level, memory %, etc.)
// reason : human-readable rationale string
typedef void (*AiTuneHandler)(const char *action, const char *target,
                               uint32_t value, const char *reason);

// Register a handler that ai_poll_cmd() calls on each TUNE decision.
// Pass NULL to clear the handler.
void ai_set_tune_handler(AiTuneHandler handler);

// Syscall dispatcher
uint32_t ai_syscall(uint32_t num, uint32_t a1, uint32_t a2, uint32_t a3);

// Build event data string from parts: "part1|part2|part3" into buf.
// Returns pointer to buf for convenience. Truncates if overflow.
char *ai_build_event(char *buf, size_t max,
                     const char *p1, const char *p2, const char *p3);

// Remediation handler callback type.
// action : "log" | "throttle" | "kill" | "revoke" | "suspend"
// target : PID as string, or "all"
// param  : action-specific parameter (e.g., quantum ms, capability name)
typedef void (*AiRemediateHandler)(const char *action, const char *target,
                                    const char *param);

void ai_set_remediate_handler(AiRemediateHandler handler);

// Force bridge status (used by in-kernel AI engine to override COM2 offline)
void ai_bridge_set_status(AiBridgeStatus status);

// =============================================================================
// In-Kernel Rust AI Engine (Phase 10)
// =============================================================================

// Initialize the in-kernel AI engine (pass kernel scheduler pointer)
void ai_kernel_engine_init(void *scheduler);

// Feed an event into the AI engine (called from exception/syscall/module handlers)
void ai_kernel_engine_feed(const char *event_type, const char *data, uint64_t now);

// Feed event using numeric code — faster, avoids string parsing (Phase 15)
void ai_kernel_engine_feed_code(uint8_t event_code, const char *data, uint64_t now);

// Periodic tick — call from timer IRQ every 50 ticks (~500ms)
void ai_kernel_engine_tick(uint64_t now);

// Query engine stats
uint64_t ai_kernel_engine_event_count(void);
uint32_t ai_kernel_engine_anomaly_count(void);
uint32_t ai_kernel_engine_active_procs(void);

// =============================================================================
// Policy System (Phase 12)
// =============================================================================

// Load a binary policy blob (VPOL format) into the AI engine.
// Returns 1 on success, 0 on failure.
uint32_t ai_kernel_engine_load_policy(const uint8_t *blob, size_t len);

// Get the currently loaded policy version.
uint16_t ai_kernel_engine_policy_version(void);

// Load policy from os.img disk (sector 4096) via ATA PIO.
void policy_load_from_disk(void);

// =============================================================================
// Policy Enforcement (Phase 13)
// =============================================================================

// Check if a command is allowed at the given privilege level.
// Returns required privilege (0=root, 50=admin, 100=user), or 255 if no rule.
uint8_t ai_kernel_engine_check_access(const char *command, size_t len);

// Get number of loaded access rules.
uint32_t ai_kernel_engine_access_rule_count(void);

#ifdef __cplusplus
}
#endif

#endif // VERNISOS_AI_BRIDGE_H
