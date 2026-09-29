// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

/*
 * This is the pattern an IREE dispatch wants, done by hand on four cores: each
 * core stages its own tile into its own scratchpad, works out of fast local
 * memory, and writes the tile back. It exists to answer one question with a
 * measurement rather than an assumption - *when* does staging pay?
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cluster_team.h"

#define N_ELEMS 512u /* 128 words per core = 512 B tiles */

#ifndef PASSES
#define PASSES 8u
#endif

static volatile uint32_t src[N_ELEMS];
static volatile uint32_t dst_l2[N_ELEMS];
static volatile uint32_t dst_l1[N_ELEMS];
static volatile uint32_t cycles_l2[NUM_CORES];
static volatile uint32_t cycles_l1[NUM_CORES];

static cl_team_t team;

static void sweep(volatile uint32_t *buf, uint32_t n) {
    for (uint32_t p = 0u; p < PASSES; p++) {
        for (uint32_t i = 0u; i < n; i++) {
            buf[i] = buf[i] * 3u + 1u;
        }
    }
}

int main(void) {
    uint32_t hart = cl_core_idx();
    if (hart >= cl_core_num()) {
        return 1;
    }

    if (cl_is_primary()) {
        cl_team_init(&team);
        for (uint32_t i = 0u; i < N_ELEMS; i++) {
            src[i] = i + 1u;
            dst_l2[i] = 0u;
            dst_l1[i] = 0u;
        }
        cl_fence();
    }
    cl_team_barrier(&team);

    cl_range_t r = cl_range_split(N_ELEMS, hart, cl_core_num());
    uint32_t words = r.end - r.begin;
    uint32_t bytes = words * 4u;

    {
        uint32_t c0 = cl_read_mcycle();
        for (uint32_t i = r.begin; i < r.end; i++) {
            dst_l2[i] = src[i];
        }
        sweep(&dst_l2[r.begin], words);
        cycles_l2[hart] = cl_read_mcycle() - c0;
    }

    cl_fence();
    cl_team_barrier(&team);

    {
        volatile uint32_t *tile = cl_l1_base();
        uint32_t c0 = cl_read_mcycle();

        cl_fence();
        cl_dma_memcpy((void *)tile, (const void *)&src[r.begin], bytes);

        cl_dma_wait();

        sweep(tile, words);

        cl_fence();
        cl_dma_memcpy((void *)&dst_l1[r.begin], (const void *)tile, bytes);
        cl_dma_wait();

        cycles_l1[hart] = cl_read_mcycle() - c0;
    }

    cl_fence();
    cl_team_barrier(&team);

    if (!cl_is_primary()) {
        return 0;
    }

    uint32_t fail = 0u;
    for (uint32_t i = 0u; i < N_ELEMS; i++) {
        uint32_t expect = src[i];
        for (uint32_t p = 0u; p < PASSES; p++) {
            expect = expect * 3u + 1u;
        }
        if (dst_l2[i] != expect || dst_l1[i] != expect) {
            printf("elem %" PRIu32 ": l2=%" PRIu32 " l1=%" PRIu32
                   " expected %" PRIu32 "\n",
                   i, dst_l2[i], dst_l1[i], expect);
            fail = 1u;
            break;
        }
    }

    uint32_t max_l2 = 0u, max_l1 = 0u;
    for (uint32_t c = 0u; c < cl_core_num(); c++) {
        if (cycles_l2[c] > max_l2) {
            max_l2 = cycles_l2[c];
        }
        if (cycles_l1[c] > max_l1) {
            max_l1 = cycles_l1[c];
        }
    }

    printf("%" PRIu32 " words/core, %" PRIu32 " passes\n",
           (uint32_t)(N_ELEMS / NUM_CORES), (uint32_t)PASSES);
    for (uint32_t c = 0u; c < cl_core_num(); c++) {
        printf("  core %" PRIu32 ": in L2 %8" PRIu32
               " cyc   staged in L1 %8" PRIu32 " cyc\n",
               c, cycles_l2[c], cycles_l1[c]);
    }
    printf("makespan: %" PRIu32 " -> %" PRIu32 " cycles\n", max_l2, max_l1);

    if (max_l1 >= max_l2) {
        puts("note: staging did not win at this pass count");
    }

    if (fail == 0u) {
        puts("L1 STAGING DEMO PASS");
        return 0;
    }
    puts("L1 STAGING DEMO FAIL");
    return 1;
}
