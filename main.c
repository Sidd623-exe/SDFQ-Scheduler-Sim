#include <stdio.h>
#include "task.h"
extern void heap_push(Task* t);
extern void run_simulation(int total_ticks);
int main() {
Task task1 = { .id = 1, .vruntime = 10, .burst_time = 3, .state = TASK_READY };
    Task task2 = { .id = 2, .vruntime = 0,  .burst_time = 4, .state = TASK_READY };

    heap_push(&task1);
    heap_push(&task2);

    run_simulation(10);
}