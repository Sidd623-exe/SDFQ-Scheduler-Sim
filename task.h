#ifndef TASK_H
#define TASK_H

/*
 * task.h — Data structures and constants for CFS / SDFQ simulation.
 */

#include <stdbool.h>

/* ── CFS Tunables ─────────────────────────────────────────────── */
#define SCHED_LATENCY      6       /* target latency (ticks)      */
#define MIN_GRANULARITY    1       /* minimum time-slice (ticks)  */
#define NICE_0_WEIGHT      1024    /* baseline weight at nice 0   */
#define MAX_TASKS          1024
#define MAX_TICKS          2000    /* upper bound for telemetry   */

/* ── Task States ──────────────────────────────────────────────── */
typedef enum {
    TASK_NEW,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} TaskState;

/* ── Task Control Block ───────────────────────────────────────── */
typedef struct {
    int       id;
    int       arrival_time;
    int       burst_time;        /* total CPU ticks needed          */
    TaskState state;

    int       nice;              /* −20 … +19                       */
    int       weight;            /* looked-up from cfs_weight_table */
    long      vruntime;          /* weighted virtual runtime        */

    int       time_slice;        /* remaining ticks in current slice*/

    /* I/O simulation */
    int       io_freq;           /* block every N cpu ticks (0=none)*/
    int       io_duration;       /* ticks spent blocked             */
    int       current_cpu_run;   /* cpu ticks since last I/O block  */
    int       sleep_start_tick;  /* tick when I/O block began       */
    int       sleep_tick_left;   /* remaining I/O block ticks       */
} Task;

/* ── CFS Weight Table (defined in scheduler.c) ───────────────── */
extern const int cfs_weight_table[40];

/* ── Generator (generator.c) ─────────────────────────────────── */
Task* generate_tasks(int num_tasks);

/* ── Scheduler (scheduler.c) ─────────────────────────────────── */
void run_cfs_simulation(Task *tasks, int num_tasks, int total_ticks,
                        bool use_sdfq);
void plot_telemetry(Task *tasks, int num_tasks, bool sdfq_enabled);

#endif /* TASK_H */