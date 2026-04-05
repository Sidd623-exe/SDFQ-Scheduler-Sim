
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "task.h"

#define NUM_TASKS  20
#define SIM_TICKS  500

int main(void) {
    srand((unsigned)time(NULL));

    Task *tasks = generate_tasks(NUM_TASKS);
    if (!tasks) {
        fprintf(stderr, "Failed to allocate tasks.\n");
        return 1;
    }

    /* ── Deep copy for second run ───────────────────────────── */
    Task *tasks_copy = (Task *)malloc(NUM_TASKS * sizeof(Task));
    if (!tasks_copy) {
        fprintf(stderr, "Failed to allocate task copy.\n");
        free(tasks);
        return 1;
    }
    memcpy(tasks_copy, tasks, NUM_TASKS * sizeof(Task));

    /* ── Print generated workload ───────────────────────────── */
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     CFS / SDFQ SCHEDULER SIMULATION                ║\n");
    printf("║     Tasks: %-4d       Ticks: %-4d                  ║\n",
           NUM_TASKS, SIM_TICKS);
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    printf("Generated workload:\n");
    printf("  %-4s %-6s %-6s %-6s %-8s %-8s %-8s\n",
           "ID", "Nice", "Weight", "Burst", "Arrival", "IO freq", "IO dur");
    printf("  ────────────────────────────────────────────────────\n");
    for (int i = 0; i < NUM_TASKS; i++) {
        printf("  %-4d %-+6d %-6d %-6d %-8d %-8d %-8d\n",
               tasks[i].id, tasks[i].nice, tasks[i].weight,
               tasks[i].burst_time, tasks[i].arrival_time,
               tasks[i].io_freq, tasks[i].io_duration);
    }

    /* ── Run 1: Standard CFS ────────────────────────────────── */
    printf("\n");
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║           === RUNNING STANDARD CFS ===              ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    run_cfs_simulation(tasks, NUM_TASKS, SIM_TICKS, false);
    plot_telemetry(tasks, NUM_TASKS, false);

    /* ── Run 2: SDFQ Warp ───────────────────────────────────── */
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║           === RUNNING SDFQ WARP ===                 ║\n");
    printf("╚══════════════════════════════════════════════════════╝\n\n");

    run_cfs_simulation(tasks_copy, NUM_TASKS, SIM_TICKS, true);
    plot_telemetry(tasks_copy, NUM_TASKS, true);

    /* ── Cleanup ────────────────────────────────────────────── */
    free(tasks);
    free(tasks_copy);

    printf("Simulation complete.\n");
    return 0;
}
