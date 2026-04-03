#ifndef TASK_H
#define TASK_H

typedef enum{
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} TaskState;

typedef struct {
    int id;
    long vruntime;
    int burst_time;
    TaskState state;
} Task;

#endif // TASK_H