// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-FileCopyrightText: 2026 Embecosm Limited
// SPDX-License-Identifier: Apache-2.0

/*
 * dispatch-demo.c - core 0 hands work to all four cores.
 *
 * Cores 1-3 sit in cl_team_worker_loop() for the whole program. Core 0
 * runs normal code and calls cl_team_dispatch() / cl_team_join() whenever
 * it wants a function run on every core.
 *
 *   make APP=dispatch-demo run
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cluster_team.h"

#define N_ELEMS 256u
#define TIMED_DISPATCHES 16u
#define BARRIERS_PER_TASK 16u

static uint32_t vec_a[N_ELEMS];
static uint32_t vec_b[N_ELEMS];
static uint32_t partial[NUM_CORES];

static cl_dispatch_t disp;

typedef struct {
    uint32_t scale;
} scale_args_t;

static void kernel_scale(void *arg, uint32_t core) {
    const scale_args_t *a = arg;
    cl_range_t r = cl_range_split(N_ELEMS, core, cl_core_num());

    for (uint32_t i = r.begin; i < r.end; i++) {
        vec_b[i] = vec_a[i] * a->scale;
    }
}

static void kernel_partial_sum(void *arg, uint32_t core) {
    (void)arg;
    cl_range_t r = cl_range_split(N_ELEMS, core, cl_core_num());

    uint32_t sum = 0u;
    for (uint32_t i = r.begin; i < r.end; i++) {
        sum += vec_b[i];
    }
    partial[core] = sum;
}

static void kernel_noop(void *arg, uint32_t core) {
    (void)arg;
    (void)core;
}

static void kernel_barriers(void *arg, uint32_t core) {
    (void)arg;
    (void)core;
    for (uint32_t i = 0u; i < BARRIERS_PER_TASK; i++) {
        cl_team_barrier(&disp.team);
    }
}

int main(void) {
    if (!cl_is_primary()) {
        cl_team_worker_loop(&disp);
        return 0;
    }

    for (uint32_t i = 0u; i < N_ELEMS; i++) {
        vec_a[i] = i + 1u;
    }
    cl_dispatch_init(&disp);

    scale_args_t sargs = {3u};
    cl_team_dispatch(&disp, kernel_scale, &sargs);
    cl_team_join(&disp);

    cl_team_dispatch(&disp, kernel_partial_sum, 0);
    cl_team_join(&disp);

    uint32_t total = 0u;
    for (uint32_t c = 0u; c < cl_core_num(); c++) {
        total += partial[c];
    }
    uint32_t expected = 3u * (N_ELEMS * (N_ELEMS + 1u) / 2u);

    /* TICKS rather than mcycle: mcycle stops while core 0 sleeps. */
    uint32_t t0 = cl_read_ticks();
    for (uint32_t i = 0u; i < TIMED_DISPATCHES; i++) {
        cl_team_dispatch(&disp, kernel_noop, 0);
        cl_team_join(&disp);
    }
    uint32_t round_trip = (cl_read_ticks() - t0) / TIMED_DISPATCHES;

    uint32_t t1 = cl_read_ticks();
    cl_team_dispatch(&disp, kernel_barriers, 0);
    cl_team_join(&disp);
    uint32_t barrier_cost =
        (cl_read_ticks() - t1 - round_trip) / BARRIERS_PER_TASK;

    cl_team_shutdown(&disp);

    printf("dispatch round trip = %" PRIu32 " cycles (empty kernel, %" PRIu32
           " cores)\n",
           round_trip, cl_core_num());
    printf("team barrier        = %" PRIu32 " cycles\n", barrier_cost);
    printf("sum=%" PRIu32 " expected=%" PRIu32 "\n", total, expected);

    puts(total == expected ? "DISPATCH DEMO PASS" : "DISPATCH DEMO FAIL");
    return total == expected ? 0 : 1;
}
