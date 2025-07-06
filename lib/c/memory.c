#include "../h/memory.h"
#include <stdio.h>

int main() {
    struct Scheduler* sched = memory_scheduler_new();
    if (!sched) {
        printf("Failed to create scheduler\n");
        return 1;
    }
    size_t pid = memory_scheduler_create_process(sched, 10, "init");
    printf("Created process with PID: %zu\n", pid);
    size_t count = memory_scheduler_get_process_count(sched);
    printf("Process count: %zu\n", count);
    memory_scheduler_terminate_current(sched, 0);
    memory_scheduler_cleanup_zombies(sched);
    // ...
    return 0;
} 