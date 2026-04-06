#ifndef VERNISOS_SCHEDULER_H
#define VERNISOS_SCHEDULER_H

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

// Enum FfiProcessState
typedef enum {
    FFI_PROCESS_NEW,
    FFI_PROCESS_STANDBY,
    FFI_PROCESS_RUNNING,
    FFI_PROCESS_WAITING,
    FFI_PROCESS_SUSPENDED,
    FFI_PROCESS_TERMINATED,
    FFI_PROCESS_ZOMBIE
} FfiProcessState;

// Struct FfiProcessInfo
typedef struct {
    size_t pid;
    FfiProcessState state;
    uint8_t priority;
    int8_t nice;
    uint64_t cpu_time_secs;
} FfiProcessInfo;

// Struct SchedulerStats
typedef struct {
    uint64_t uptime;
    size_t total_processes_created;
    size_t current_process_count;
    size_t context_switches;
    double cpu_utilization;
    uint64_t idle_time;
    size_t running_processes;
    size_t standby_processes;
    size_t waiting_processes;
} SchedulerStats;

// PsRow — compact snapshot for CLI ps command
typedef struct {
    size_t  pid;
    size_t  ppid;
    uint8_t state;      // 0=New 1=Standby 2=Running 3=Waiting 4=Suspended 5=Terminated 6=Zombie
    uint8_t priority;
    uint8_t ptype;      // 0=Kernel 1=System 2=User
    uint8_t ring;       // 0-3
    uint64_t cpu_time_ms;   // Total CPU time in milliseconds
    size_t  mem_rss;        // Resident Set Size (bytes)
    size_t  mem_virt;       // Virtual memory size (bytes)
    uint64_t uptime_secs;   // Seconds since process creation
    char    command[32];
} PsRow;

// Forward declaration
struct Scheduler;

// ps FFI
size_t scheduler_get_pid_list(const struct Scheduler *sched, size_t *pids_out, size_t max_count);
bool   scheduler_get_ps_row(const struct Scheduler *sched, size_t pid, PsRow *out);

// Kernel-side accessor (implemented in kernel_x86.c / kernel_x64.c)
struct Scheduler *get_kernel_scheduler(void);

// FFI functions
struct Scheduler* scheduler_new(void);
void scheduler_free(struct Scheduler* ptr);
size_t scheduler_create_process(struct Scheduler* sched, uint8_t priority, const char* command);
size_t scheduler_schedule(struct Scheduler* sched);
void scheduler_terminate_current(struct Scheduler* sched, int32_t exit_code);int32_t scheduler_get_exit_code(const struct Scheduler* sched, size_t pid);bool scheduler_get_process_info(const struct Scheduler* sched, size_t pid, FfiProcessInfo* out_info);
void scheduler_block_current(struct Scheduler* sched, const char* reason);
bool scheduler_wake_process(struct Scheduler* sched, size_t pid);
bool scheduler_suspend_process(struct Scheduler* sched, size_t pid);
bool scheduler_resume_process(struct Scheduler* sched, size_t pid);
bool scheduler_kill_process(struct Scheduler* sched, size_t pid);
size_t scheduler_get_process_count(const struct Scheduler* sched);
size_t scheduler_get_running_process_count(const struct Scheduler* sched);
size_t scheduler_get_standby_process_count(const struct Scheduler* sched);
size_t scheduler_get_waiting_process_count(const struct Scheduler* sched);
bool scheduler_get_scheduler_stats(const struct Scheduler* sched, SchedulerStats* out_stats);
size_t scheduler_cleanup_zombies(struct Scheduler* sched);
bool scheduler_set_priority(struct Scheduler* sched, size_t pid, uint8_t priority);
bool scheduler_set_nice(struct Scheduler* sched, size_t pid, int8_t nice);
const void* scheduler_get_current_process(const struct Scheduler* sched);


#ifdef __cplusplus
}
#endif

#endif // VERNISOS_SCHEDULER_H 