// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

/*
 *   - All four harts enter main() together (bsp/crt0.S).
 *   - Hart 0 returning from main() ends the simulation for everyone, so every
 *     parallel region must be joined before it returns.
 *   - The only hardware sync is event-unit barrier 0, which every core in the
 *     cluster must enter. Everything below is built on it.
 *
 * Two ways to run work across the cores:
 *
 *   1. SPMD     - every hart runs the same code over its own slice.
 *                 `cl_range_split()`, then `cl_team_barrier()`.
 *   2. Dispatch - one primary hart drives workers parked in
 *                 `cl_team_worker_loop()`. `cl_team_dispatch()`
 *                 `cl_team_join()`. This is the shape IREE wants.
 */

#ifndef CLUSTER_TEAM_H
#define CLUSTER_TEAM_H

#include <stdint.h>

#include "cluster_sync.h"

static inline uint32_t cl_core_idx(void) { return cl_read_mhartid(); }

static inline uint32_t cl_core_num(void) { return (uint32_t)NUM_CORES; }

static inline int cl_is_primary(void) { return cl_read_mhartid() == 0u; }

typedef struct {
    cl_barrier_t bar;
} cl_team_t;

static inline void cl_team_init(cl_team_t *team) {
    cl_barrier_init(&team->bar);
}

static inline void cl_team_barrier(cl_team_t *team) {
    cl_barrier_wait(&team->bar);
}

typedef struct {
    uint32_t begin;
    uint32_t end; /* exclusive */
} cl_range_t;

 // Split [0, n) into `ncores` contiguous slices and return this core's.
static inline cl_range_t cl_range_split(uint32_t n, uint32_t core,
                                        uint32_t ncores) {
    cl_range_t r;
    uint32_t chunk = n / ncores;
    uint32_t rem = n % ncores;
    uint32_t extra = (core < rem) ? core : rem;

    r.begin = core * chunk + extra;
    r.end = r.begin + chunk + ((core < rem) ? 1u : 0u);
    return r;
}

typedef void (*cl_task_fn_t)(void *arg, uint32_t core_id);

typedef struct {
    cl_team_t team;
    cl_task_fn_t volatile fn;  /* NULL means "shut down" */
    void *volatile arg;
} cl_dispatch_t;

static inline void cl_dispatch_init(cl_dispatch_t *d) {
    d->fn = 0;
    d->arg = 0;
    cl_team_init(&d->team);
}

static inline void cl_team_worker_loop(cl_dispatch_t *d) {
    for (;;) {
        cl_team_barrier(&d->team);

        cl_task_fn_t fn = d->fn;
        if (fn == 0) {
            return;
        }

        fn(d->arg, cl_core_idx());

        cl_fence();
        cl_team_barrier(&d->team); /* join */
    }
}

static inline void cl_team_dispatch(cl_dispatch_t *d, cl_task_fn_t fn,
                                    void *arg) {
    d->fn = fn;
    d->arg = arg;
    cl_fence();

    cl_team_barrier(&d->team); /* release */
    fn(arg, cl_core_idx());
    cl_fence();
}

static inline void cl_team_join(cl_dispatch_t *d) { cl_team_barrier(&d->team); }

// Primary: release the workers to return from cl_team_worker_loop().
static inline void cl_team_shutdown(cl_dispatch_t *d) {
    d->fn = 0;
    cl_fence();
    cl_team_barrier(&d->team); /* release into the shutdown check */
}

static inline volatile uint32_t *cl_l1_base(void) {
    return SPM_PTR(cl_core_idx());
}

static inline uint32_t cl_l1_size(void) { return SPM_SIZE; }

#endif /* CLUSTER_TEAM_H */
