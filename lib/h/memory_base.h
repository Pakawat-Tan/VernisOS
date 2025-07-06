#ifndef VERNISOS_MEMORY_H
#define VERNISOS_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

#include "scheduler_base.h"

// Enum ProcessState
typedef enum {
    PROCESS_NEW,
    PROCESS_STANDBY,
    PROCESS_RUNNING,
    PROCESS_WAITING,
    PROCESS_SUSPENDED,
    PROCESS_TERMINATED,
    PROCESS_ZOMBIE
} ProcessState;

// Forward declaration
struct Scheduler;
struct ProcessControlBlock;

// FFI functions
struct Scheduler* memory_scheduler_new(void);
size_t memory_scheduler_create_process(struct Scheduler* sched, uint8_t priority, const char* command);
size_t memory_scheduler_get_process_count(const struct Scheduler* sched);
ssize_t memory_scheduler_schedule(struct Scheduler* sched);
void memory_scheduler_block_current(struct Scheduler* sched, const char* reason);
bool memory_scheduler_wake_process(struct Scheduler* sched, size_t pid);
void memory_scheduler_terminate_current(struct Scheduler* sched, int32_t exit_code);
bool memory_scheduler_kill_process(struct Scheduler* sched, size_t pid);
bool memory_scheduler_suspend_process(struct Scheduler* sched, size_t pid);
bool memory_scheduler_resume_process(struct Scheduler* sched, size_t pid);
const struct ProcessControlBlock* memory_scheduler_get_process_info(const struct Scheduler* sched, size_t pid);
bool memory_scheduler_get_scheduler_stats(const struct Scheduler* sched, SchedulerStats* out_stats);
size_t memory_scheduler_cleanup_zombies(struct Scheduler* sched);

#ifdef __cplusplus
}
#endif

#endif // VERNISOS_MEMORY_H 