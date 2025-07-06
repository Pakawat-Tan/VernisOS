#include "../h/scheduler.h"
#include <stdio.h>

int main() {
    struct Scheduler* sched = scheduler_new();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }
    size_t pid = scheduler_create_process(sched, 5, "shell");
    printf("Created process with PID: %zu\n", pid);
    size_t count = scheduler_get_process_count(sched);
    printf("Process count: %zu\n", count);
    scheduler_terminate_current(sched, 0);
    scheduler_cleanup_zombies(sched);
    scheduler_free(sched);
    return 0;
} 