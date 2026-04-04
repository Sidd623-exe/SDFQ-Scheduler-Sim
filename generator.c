/*
 * generator.c — Stochastic task generator for CFS / SDFQ simulation.
 *
 *   Burst times  : Gaussian  (Box-Muller,  μ=50, σ=15, min 5)
 *   I/O frequency: Exponential (Inverse Transform Sampling, λ=0.1)
 *   I/O duration : Exponential (Inverse Transform Sampling, λ=0.3)
 *   Arrival time : Uniform  [0, 100)
 *   Nice value   : Uniform  [−5, +5]
 *
 *   RNG must be seeded externally (srand in main.c).
 */

#include <stdlib.h>
#include <math.h>
#include "task.h"

/* ── helpers ──────────────────────────────────────────────────── */

/* uniform (0,1] — avoids log(0) */
static double uniform01(void) {
    double u;
    do { u = (double)rand() / RAND_MAX; } while (u == 0.0);
    return u;
}

/* Box-Muller transform → one Gaussian sample */
static double gaussian(double mean, double stddev) {
    double u1 = uniform01();
    double u2 = uniform01();
    double z  = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    return mean + stddev * z;
}

/* Inverse Transform Sampling → Exponential(λ) */
static double exponential(double lambda) {
    return -log(uniform01()) / lambda;
}

/* ── public API ───────────────────────────────────────────────── */

Task* generate_tasks(int num_tasks) {
    Task *tasks = (Task *)calloc(num_tasks, sizeof(Task));
    if (!tasks) return NULL;

    for (int i = 0; i < num_tasks; i++) {
        Task *t = &tasks[i];

        t->id    = i + 1;
        t->state = TASK_NEW;

        /* Burst: Gaussian(50, 15), clamped to [5, ∞) */
        double b = gaussian(50.0, 15.0);
        t->burst_time = (int)(b < 5.0 ? 5.0 : b);

        /* Arrival: Uniform [0, 100) */
        t->arrival_time = rand() % 100;

        /* Nice: Uniform [−5, +5] */
        t->nice   = (rand() % 11) - 5;
        int idx   = t->nice + 20;
        if (idx < 0)  idx = 0;
        if (idx > 39) idx = 39;
        t->weight = cfs_weight_table[idx];

        /* I/O frequency: ~Poisson-process inter-arrival via Exponential(λ=0.1)
         *   → mean gap ≈ 10 ticks.  0 means pure CPU-bound.                   */
        double io_gap = exponential(0.1);
        t->io_freq = (int)io_gap;
        if (t->io_freq < 2) t->io_freq = 0;   /* very short gap → no I/O */

        /* I/O duration: Exponential(λ=0.3), clamped ≥1 when io_freq > 0 */
        if (t->io_freq > 0) {
            double d = exponential(0.3);
            t->io_duration = (int)(d < 1.0 ? 1.0 : d);
        } else {
            t->io_duration = 0;
        }

        /* Initially-zero fields (calloc already zeroed, but be explicit) */
        t->vruntime        = 0;
        t->time_slice      = 0;
        t->current_cpu_run = 0;
        t->sleep_start_tick= 0;
        t->sleep_tick_left = 0;
    }
    return tasks;
}
