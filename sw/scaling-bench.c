// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-FileCopyrightText: 2026 Embecosm Limited
// SPDX-License-Identifier: Apache-2.0

/*
 * scaling-bench.c - the same work on 1, 2, 3 and 4 cores.
 *
 * compute: a register-only loop, so only instruction fetches go to memory.
 * memory:  each core sums its slice of an array in shared memory.
 *
 * Cores not in the team sleep at the barrier. Core 0 times each run with
 * TICKS and prints the speed-up over one core as a percentage.
 *
 *   make APP=scaling-bench run
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cluster_team.h"

/* Big enough that the barriers around each run are lost in the noise, small
 * enough that the simulation stays quick. 16384 words also keeps the sum
 * checked at the end within 32 bits. */
#define COMPUTE_ITERS 40000u
#define MEMORY_WORDS 16384u

enum { COMPUTE, MEMORY, NUM_WORKLOADS };

static uint32_t data[MEMORY_WORDS];
static uint32_t result[NUM_CORES];
static uint32_t ticks[NUM_WORKLOADS][NUM_CORES + 1u];
static uint32_t total[NUM_WORKLOADS][NUM_CORES + 1u];
static cl_team_t team;

static uint32_t compute(uint32_t n) {
    uint32_t a = 1u, b = 2u;
    for (uint32_t i = 0u; i < n; i++) {
        a = a * 3u + b;
        b ^= a >> 3;
    }
    return a ^ b;
}

static uint32_t sum(const uint32_t *p, uint32_t n) {
    uint32_t s = 0u;
    for (uint32_t i = 0u; i < n; i++) {
        s += p[i];
    }
    return s;
}

/* Called on every core. Cores 0..k-1 do the work; returns the time on
 * core 0. */
static uint32_t run(uint32_t workload, uint32_t k) {
    uint32_t core = cl_core_idx();
    uint32_t t0 = 0u;

    cl_team_barrier(&team);
    if (core == 0u) {
        t0 = cl_read_ticks();
    }

    if (core < k) {
        if (workload == COMPUTE) {
            cl_range_t r = cl_range_split(COMPUTE_ITERS, core, k);
            result[core] = compute(r.end - r.begin);
        } else {
            cl_range_t r = cl_range_split(MEMORY_WORDS, core, k);
            result[core] = sum(&data[r.begin], r.end - r.begin);
        }
    }

    cl_team_barrier(&team);
    return (core == 0u) ? cl_read_ticks() - t0 : 0u;
}

int main(void) {
    uint32_t core = cl_core_idx();

    if (core == 0u) {
        for (uint32_t i = 0u; i < MEMORY_WORDS; i++) {
            data[i] = i;
        }
        cl_team_init(&team);
    }

    for (uint32_t w = 0u; w < NUM_WORKLOADS; w++) {
        for (uint32_t k = 1u; k <= cl_core_num(); k++) {
            uint32_t t = run(w, k);
            if (core == 0u) {
                ticks[w][k] = t;
                for (uint32_t c = 0u; c < k; c++) {
                    total[w][k] += result[c];
                }
            }
        }
    }

    if (core != 0u) {
        return 0;
    }

    static const char *const name[] = {"compute", "memory"};
    printf("workload cores   cycles  speed-up%%\n");
    for (uint32_t w = 0u; w < NUM_WORKLOADS; w++) {
        for (uint32_t k = 1u; k <= cl_core_num(); k++) {
            printf("%-8s %5" PRIu32 " %8" PRIu32 " %9" PRIu32 "\n", name[w], k,
                   ticks[w][k], ticks[w][1] * 100u / ticks[w][k]);
        }
    }

    // The memory sums must not depend on how the array was split.
    uint32_t expected = MEMORY_WORDS * (MEMORY_WORDS - 1u) / 2u;
    int ok = 1;
    for (uint32_t k = 1u; k <= cl_core_num(); k++) {
        if (total[MEMORY][k] != expected) {
            ok = 0;
        }
    }

    puts(ok ? "SCALING BENCH PASS" : "SCALING BENCH FAIL");
    return ok ? 0 : 1;
}
