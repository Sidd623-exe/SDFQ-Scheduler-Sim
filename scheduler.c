/*
 * scheduler.c — CFS engine, SDFQ warp logic, and Gnuplot telemetry pipe.
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include "task.h"

/* Windows compatibility for popen/pclose */
#ifdef _WIN32
  #define POPEN  _popen
  #define PCLOSE _pclose
#else
  #define POPEN  popen
  #define PCLOSE pclose
#endif

/*
 * Vruntime is stored in fixed-point with 10-bit fractional part
 * to avoid integer truncation when NICE_0_WEIGHT / weight < 1.
 * All vruntime comparisons and arithmetic use this scale.
 */
#define VRT_SHIFT 10
#define VRT_SCALE (1L << VRT_SHIFT)  /* 1024 */


/* ══════════════════════════════════════════════════════════════════
 *  CFS WEIGHT TABLE  (Linux kernel sched/core.c)
 *  Index 0 = nice −20  …  Index 20 = nice 0  …  Index 39 = nice +19
 * ══════════════════════════════════════════════════════════════════ */
const int cfs_weight_table[40] = {
 /* -20 */ 88761, 71755, 56483, 46273, 36291,
 /* -15 */ 29154, 23254, 18705, 14949, 11916,
 /* -10 */  9548,  7620,  6100,  4904,  3906,
 /*  -5 */  3121,  2501,  1991,  1586,  1277,
 /*   0 */  1024,   820,   655,   526,   423,
 /*  +5 */   335,   272,   215,   172,   137,
 /* +10 */   110,    87,    70,    56,    45,
 /* +15 */    36,    29,    23,    18,    15
};

/* ══════════════════════════════════════════════════════════════════
 *  MIN-HEAP  (keyed on vruntime, O(log N) push/pop)
 * ══════════════════════════════════════════════════════════════════ */
static Task *heap[MAX_TASKS];
static int   heap_sz = 0;

static void heap_swap(int a, int b) {
    Task *t = heap[a]; heap[a] = heap[b]; heap[b] = t;
}

static void sift_up(int i) {
    while (i > 0) {
        int p = (i - 1) / 2;
        if (heap[i]->vruntime < heap[p]->vruntime) { heap_swap(i, p); i = p; }
        else break;
    }
}

static void sift_down(int i) {
    for (;;) {
        int l = 2*i+1, r = 2*i+2, s = i;
        if (l < heap_sz && heap[l]->vruntime < heap[s]->vruntime) s = l;
        if (r < heap_sz && heap[r]->vruntime < heap[s]->vruntime) s = r;
        if (s == i) break;
        heap_swap(i, s); i = s;
    }
}

static void heap_push(Task *t) {
    heap[heap_sz] = t;
    sift_up(heap_sz);
    heap_sz++;
}

static Task *heap_pop(void) {
    if (heap_sz == 0) return NULL;
    Task *top = heap[0];
    heap_sz--;
    heap[0] = heap[heap_sz];
    sift_down(0);
    return top;
}

/* ══════════════════════════════════════════════════════════════════
 *  BLOCKED LIST  (tasks waiting on I/O)
 * ══════════════════════════════════════════════════════════════════ */
static Task *blocked[MAX_TASKS];
static int   blocked_sz = 0;

/* ══════════════════════════════════════════════════════════════════
 *  TELEMETRY  (per-tick vruntime log for Gnuplot)
 * ══════════════════════════════════════════════════════════════════ */
static long telem_vrt[MAX_TASKS][MAX_TICKS];
static int  telem_ticks = 0;

/* ══════════════════════════════════════════════════════════════════
 *  CFS HELPERS
 * ══════════════════════════════════════════════════════════════════ */

static int total_heap_weight(Task *cur) {
    int w = cur ? cur->weight : 0;
    for (int i = 0; i < heap_sz; i++) w += heap[i]->weight;
    return w;
}

static int calc_slice(Task *t, int total_w) {
    if (total_w <= 0) return SCHED_LATENCY;
    int s = (int)((long)SCHED_LATENCY * t->weight / total_w);
    return s < MIN_GRANULARITY ? MIN_GRANULARITY : s;
}

static long min_vrt = 0;

static void update_min_vrt(Task *cur) {
    long v = LONG_MAX;
    if (cur && cur->state == TASK_RUNNING) v = cur->vruntime;
    if (heap_sz > 0 && heap[0]->vruntime < v) v = heap[0]->vruntime;
    if (v != LONG_MAX && v > min_vrt) min_vrt = v;
}

/* ══════════════════════════════════════════════════════════════════
 *  SIMULATION
 * ══════════════════════════════════════════════════════════════════ */

void run_cfs_simulation(Task *tasks, int num_tasks, int total_ticks,
                        bool use_sdfq) {
    /* reset global state */
    heap_sz    = 0;
    blocked_sz = 0;
    min_vrt    = 0;
    telem_ticks = 0;
    memset(telem_vrt, 0, sizeof(telem_vrt));

    Task *cur = NULL;

    printf("  Latency=%d  MinGran=%d  Ticks=%d  SDFQ=%s\n\n",
           SCHED_LATENCY, MIN_GRANULARITY, total_ticks,
           use_sdfq ? "ON" : "OFF");

    for (int tick = 0; tick < total_ticks; tick++) {

        /* ── Phase 1: Arrivals ──────────────────────────────── */
        for (int i = 0; i < num_tasks; i++) {
            if (tasks[i].state == TASK_NEW && tasks[i].arrival_time == tick) {
                tasks[i].state    = TASK_READY;
                tasks[i].vruntime = min_vrt;  /* scaled */

                printf("  [%4d] ARRIVE  Task %2d  nice=%+d w=%d burst=%d",
                       tick, tasks[i].id, tasks[i].nice,
                       tasks[i].weight, tasks[i].burst_time);
                if (tasks[i].io_freq > 0)
                    printf("  io=%d/%d", tasks[i].io_freq, tasks[i].io_duration);
                printf("\n");
                heap_push(&tasks[i]);
            }
        }

        /* ── Phase 2: I/O Wakeups ──────────────────────────── */
        for (int i = blocked_sz - 1; i >= 0; i--) {
            blocked[i]->sleep_tick_left--;
            if (blocked[i]->sleep_tick_left <= 0) {
                Task *t = blocked[i];
                t->state = TASK_READY;
                t->current_cpu_run = 0;

                if (use_sdfq) {
                    /* SDFQ Warp: boost based on sleep duration */
                    int sleep_dur = tick - t->sleep_start_tick;
                    long boost    = (long)(sleep_dur * 0.5 * VRT_SCALE);
                    t->vruntime   = min_vrt - boost;
                    printf("  [%4d] WAKE(S) Task %2d  sleep=%d boost=%ld vrt→%ld\n",
                           tick, t->id, sleep_dur, boost / VRT_SCALE,
                           t->vruntime / VRT_SCALE);
                } else {
                    /* Standard CFS: snap to max(old, min_vrt − latency) */
                    long comp = min_vrt - (long)SCHED_LATENCY * VRT_SCALE;
                    if (comp > t->vruntime) t->vruntime = comp;
                    printf("  [%4d] WAKE    Task %2d  vrt→%ld\n",
                           tick, t->id, t->vruntime / VRT_SCALE);
                }
                heap_push(t);
                blocked[i] = blocked[blocked_sz - 1];
                blocked_sz--;
            }
        }

        /* ── Phase 3: Pick next (CPU idle) ─────────────────── */
        if (cur == NULL && heap_sz > 0) {
            cur = heap_pop();
            cur->state = TASK_RUNNING;
            cur->time_slice = calc_slice(cur, total_heap_weight(cur));
            printf("  [%4d] SCHED   Task %2d  vrt=%ld slice=%d\n",
                   tick, cur->id, cur->vruntime, cur->time_slice);
        }

        /* ── Phase 4: Preemption (slice expired) ───────────── */
        if (cur && cur->time_slice <= 0 && heap_sz > 0) {
            printf("  [%4d] PREEMPT Task %2d\n", tick, cur->id);
            cur->state = TASK_READY;
            heap_push(cur);

            cur = heap_pop();
            cur->state = TASK_RUNNING;
            cur->time_slice = calc_slice(cur, total_heap_weight(cur));
            printf("  [%4d] SCHED   Task %2d  vrt=%ld slice=%d\n",
                   tick, cur->id, cur->vruntime / VRT_SCALE, cur->time_slice);
        }

        /* ── Phase 5: Execute one tick ─────────────────────── */
        if (cur) {
            cur->burst_time--;
            cur->time_slice--;
            cur->current_cpu_run++;
            cur->vruntime += (long)NICE_0_WEIGHT * VRT_SCALE / cur->weight;
        }

        /* ── Phase 6: I/O block check ──────────────────────── */
        if (cur && cur->burst_time > 0
            && cur->io_freq > 0
            && cur->current_cpu_run > 0
            && cur->current_cpu_run % cur->io_freq == 0) {
            cur->state            = TASK_BLOCKED;
            cur->sleep_tick_left  = cur->io_duration;
            cur->sleep_start_tick = tick;
            printf("  [%4d] IO_BLK  Task %2d  for %d ticks\n",
                   tick, cur->id, cur->io_duration);
            blocked[blocked_sz++] = cur;
            cur = NULL;
        }

        /* ── Phase 7: Termination ──────────────────────────── */
        if (cur && cur->burst_time <= 0) {
            cur->state = TASK_TERMINATED;
            printf("  [%4d] DONE    Task %2d  vrt=%ld\n",
                   tick, cur->id, cur->vruntime / VRT_SCALE);
            cur = NULL;
        }

        /* ── Phase 8: Update min_vruntime ──────────────────── */
        update_min_vrt(cur);

        /* ── Phase 9: Record telemetry ─────────────────────── */
        if (tick < MAX_TICKS) {
            for (int i = 0; i < num_tasks; i++)
                telem_vrt[i][tick] = tasks[i].vruntime;
            telem_ticks = tick + 1;
        }

        /* ── Phase 10: Accumulate wait time ────────────── */
        for (int i = 0; i < heap_sz; i++) {
            /* Tasks sitting in the ready queue are waiting */
            (void)heap[i];
        }

        /* Check if all tasks are done */
        int alive = 0;
        for (int i = 0; i < num_tasks; i++)
            if (tasks[i].state != TASK_TERMINATED) alive++;
        if (alive == 0) {
            printf("  [%4d] ALL TASKS COMPLETED\n", tick);
            telem_ticks = tick + 1;
            break;
        }
    }

    printf("\n  %-4s %-6s %-6s %-8s %-10s  %s\n",
           "ID", "Nice", "Weight", "Burst", "Vruntime", "State");
    printf("  ─────────────────────────────────────────────\n");
    for (int i = 0; i < num_tasks; i++) {
        Task *t = &tasks[i];
        const char *s = t->state == TASK_TERMINATED ? "DONE" :
                        t->state == TASK_READY      ? "READY" :
                        t->state == TASK_RUNNING     ? "RUN" :
                        t->state == TASK_BLOCKED     ? "BLOCK" : "NEW";
        printf("  %-4d %-+6d %-6d %-8d %-10ld  %s\n",
               t->id, t->nice, t->weight, t->burst_time,
               t->vruntime / VRT_SCALE, s);
    }
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════════
 *  GNUPLOT TELEMETRY PIPE
 * ══════════════════════════════════════════════════════════════════ */

void plot_telemetry(Task *tasks, int num_tasks, bool sdfq_enabled) {
    FILE *gp = POPEN("gnuplot -persistent", "w");
    if (!gp) {
        fprintf(stderr,
            "  [NOTE] Gnuplot not found — skipping plot.\n"
            "  Install Gnuplot to see vruntime graphs.\n\n");
        return;
    }

    fprintf(gp, "set title '%s — Vruntime over Time'\n",
            sdfq_enabled ? "SDFQ Warp" : "Standard CFS");
    fprintf(gp, "set xlabel 'Ticks'\n");
    fprintf(gp, "set ylabel 'Vruntime'\n");
    fprintf(gp, "set grid\n");
    fprintf(gp, "set key outside right\n");

    /* Build the plot command for all tasks */
    fprintf(gp, "plot ");
    for (int i = 0; i < num_tasks; i++) {
        fprintf(gp, "'-' with lines title 'Task %d (n=%+d)'",
                tasks[i].id, tasks[i].nice);
        if (i < num_tasks - 1) fprintf(gp, ", ");
    }
    fprintf(gp, "\n");

    /* Pipe data for each task (descale vruntime) */
    for (int i = 0; i < num_tasks; i++) {
        for (int t = 0; t < telem_ticks; t++)
            fprintf(gp, "%d %ld\n", t, telem_vrt[i][t] / VRT_SCALE);
        fprintf(gp, "e\n");
    }

    fflush(gp);
    PCLOSE(gp);
    printf("  [Gnuplot window opened]\n\n");
}