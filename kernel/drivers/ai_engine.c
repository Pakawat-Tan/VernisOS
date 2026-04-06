// ai_engine.c — C integration layer for in-kernel Rust AI engine
//
// Wraps Rust FFI functions and integrates with kernel subsystems.

#include "ai_bridge.h"
#include <stdint.h>
#include <stddef.h>

// Rust FFI declarations
extern void    *ai_engine_new(void);
extern void     ai_engine_feed_event(void *engine,
                    const uint8_t *evt_type, size_t evt_type_len,
                    const uint8_t *data, size_t data_len,
                    uint64_t now_ticks);
extern void     ai_engine_feed_event_code(void *engine,
                    uint8_t event_code,
                    const uint8_t *data, size_t data_len,
                    uint64_t now_ticks);
extern void     ai_engine_tick(void *engine, uint64_t now_ticks);
extern void     ai_engine_set_tune_cb(void *engine, void *cb);
extern void     ai_engine_set_remediate_cb(void *engine, void *cb);
extern uint64_t ai_engine_event_count(const void *engine);
extern uint32_t ai_engine_anomaly_count(const void *engine);
extern uint32_t ai_engine_active_procs(const void *engine);
extern uint32_t ai_engine_decision_count(const void *engine);
extern void     ai_engine_free(void *engine);
extern uint32_t ai_engine_load_policy(void *engine, const uint8_t *blob, size_t len);
extern uint16_t ai_engine_policy_version(const void *engine);
extern uint8_t  ai_engine_check_access(const void *engine, const uint8_t *cmd, size_t cmd_len);
extern uint32_t ai_engine_access_rule_count(const void *engine);

// External kernel functions
extern void serial_print(const char *s);
extern void serial_print_dec(uint32_t val);
extern void *scheduler_new(void);
extern void  scheduler_set_quantum(void *sched, uint32_t quantum);
extern void  scheduler_set_priority(void *sched, uint32_t pid, uint8_t priority);

// Global AI engine instance
static void *g_ai_engine = NULL;

// Reference to kernel scheduler (set during init)
static void *g_kernel_scheduler = NULL;

// Kernel tick counter (updated by timer IRQ via ai_kernel_engine_feed)
static uint64_t g_ticks = 0;

// =============================================================================
// Callbacks from Rust → C kernel
// =============================================================================

static void kernel_ai_tune_cb(const uint8_t *action, size_t action_len,
                               const uint8_t *target, size_t target_len,
                               uint32_t value) {
    (void)target; (void)target_len;
    if (!g_kernel_scheduler) return;

    // Check action: "SCHED_QUANTUM" or "SCHED_PRIO"
    if (action_len >= 13 && action[6] == 'Q') {
        if (value < 1) value = 1;
        if (value > 200) value = 200;
        scheduler_set_quantum(g_kernel_scheduler, value);
    } else if (action_len >= 10 && action[6] == 'P') {
        scheduler_set_priority(g_kernel_scheduler, 1, (uint8_t)value);
    }
}

static void kernel_ai_remediate_cb(const uint8_t *action, size_t action_len,
                                    uint32_t pid, uint32_t param) {
    if (!g_kernel_scheduler) return;

    // "kill" → set priority 0 (equivalent to terminate)
    if (action_len >= 4 && action[0] == 'k' && action[1] == 'i') {
        if (pid > 0) {
            scheduler_set_priority(g_kernel_scheduler, pid, 0);
        }
    }
    // "throttle" → set quantum
    else if (action_len >= 8 && action[0] == 't' && action[1] == 'h') {
        uint32_t q = param > 0 ? param : 25;
        if (q > 200) q = 200;
        scheduler_set_quantum(g_kernel_scheduler, q);
    }
}

// =============================================================================
// Public API — called from kernel C code
// =============================================================================

void ai_kernel_engine_init(void *scheduler) {
    g_kernel_scheduler = scheduler;
    g_ticks = 0;

    g_ai_engine = ai_engine_new();
    if (!g_ai_engine) {
        serial_print("[ai-engine] ERROR: failed to create AI engine\n");
        return;
    }

    ai_engine_set_tune_cb(g_ai_engine, (void*)kernel_ai_tune_cb);
    ai_engine_set_remediate_cb(g_ai_engine, (void*)kernel_ai_remediate_cb);

    // Override COM2 offline status — AI is available in-kernel
    ai_bridge_set_status(AI_STATUS_READY);

    serial_print("[ai-engine] In-kernel Rust AI engine ready\n");
}

void ai_kernel_engine_feed(const char *event_type, const char *data, uint64_t now) {
    if (!g_ai_engine) return;
    g_ticks = now;

    size_t et_len = 0;
    while (event_type[et_len]) et_len++;
    size_t d_len = 0;
    while (data[d_len]) d_len++;

    ai_engine_feed_event(g_ai_engine,
        (const uint8_t*)event_type, et_len,
        (const uint8_t*)data, d_len,
        now);
}

void ai_kernel_engine_feed_code(uint8_t event_code, const char *data, uint64_t now) {
    if (!g_ai_engine) return;
    g_ticks = now;

    size_t d_len = 0;
    while (data[d_len]) d_len++;

    ai_engine_feed_event_code(g_ai_engine,
        event_code,
        (const uint8_t*)data, d_len,
        now);
}

void ai_kernel_engine_tick(uint64_t now) {
    if (!g_ai_engine) return;
    g_ticks = now;
    ai_engine_tick(g_ai_engine, now);
}

uint64_t ai_kernel_engine_event_count(void) {
    if (!g_ai_engine) return 0;
    return ai_engine_event_count(g_ai_engine);
}

uint32_t ai_kernel_engine_anomaly_count(void) {
    if (!g_ai_engine) return 0;
    return ai_engine_anomaly_count(g_ai_engine);
}

uint32_t ai_kernel_engine_active_procs(void) {
    if (!g_ai_engine) return 0;
    return ai_engine_active_procs(g_ai_engine);
}

uint32_t ai_kernel_engine_load_policy(const uint8_t *blob, size_t len) {
    if (!g_ai_engine || !blob || len < 8) return 0;
    uint32_t result = ai_engine_load_policy(g_ai_engine, blob, len);
    if (result) {
        serial_print("[ai-engine] Policy loaded v");
        serial_print_dec((uint32_t)ai_engine_policy_version(g_ai_engine));
        serial_print("\n");
    } else {
        serial_print("[ai-engine] Policy load FAILED\n");
    }
    return result;
}

uint16_t ai_kernel_engine_policy_version(void) {
    if (!g_ai_engine) return 0;
    return ai_engine_policy_version(g_ai_engine);
}

uint8_t ai_kernel_engine_check_access(const char *command, size_t len) {
    if (!g_ai_engine || !command || len == 0) return 255;
    return ai_engine_check_access(g_ai_engine, (const uint8_t *)command, len);
}

uint32_t ai_kernel_engine_access_rule_count(void) {
    if (!g_ai_engine) return 0;
    return ai_engine_access_rule_count(g_ai_engine);
}
