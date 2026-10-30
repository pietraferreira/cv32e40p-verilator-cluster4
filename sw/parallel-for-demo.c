// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-FileCopyrightText: 2026 Embecosm Limited
// SPDX-License-Identifier: Apache-2.0

/*
 * parallel-for-demo.c - split a loop across the four cores.
 *
 * Each core handles its own slice of [0, N) from cl_range_split(). N is not
 * a multiple of four, so the slices can't all be the same length; they
 * should differ by at most one.
 *
 *   make APP=parallel-for-demo run
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cluster_team.h"

#define N_ELEMS 101u

static volatile uint32_t out[N_ELEMS];
static volatile uint32_t slice_begin[NUM_CORES];
static volatile uint32_t slice_end[NUM_CORES];
static cl_team_t team;

int main(void) {
    uint32_t core = cl_core_idx();

    if (cl_is_primary()) {
        cl_team_init(&team);
    }

    cl_range_t r = cl_range_split(N_ELEMS, core, cl_core_num());
    slice_begin[core] = r.begin;
    slice_end[core] = r.end;
    for (uint32_t i = r.begin; i < r.end; i++) {
        out[i] = i * i + 1u; /* never 0, so a missed element shows up */
    }

    cl_team_barrier(&team);

    if (!cl_is_primary()) {
        return 0;
    }

    int ok = 1;
    for (uint32_t i = 0u; i < N_ELEMS; i++) {
        if (out[i] != i * i + 1u) {
            printf("bad value at %" PRIu32 "\n", i);
            ok = 0;
        }
    }

    uint32_t next = 0u;
    uint32_t longest = 0u;
    uint32_t shortest = N_ELEMS;
    for (uint32_t c = 0u; c < cl_core_num(); c++) {
        uint32_t len = slice_end[c] - slice_begin[c];
        printf("  core %" PRIu32 ": [%" PRIu32 ", %" PRIu32 ") len=%" PRIu32
               "\n",
               c, slice_begin[c], slice_end[c], len);
        if (slice_begin[c] != next) {
            ok = 0;
        }
        next = slice_end[c];
        if (len > longest) {
            longest = len;
        }
        if (len < shortest) {
            shortest = len;
        }
    }
    if (next != N_ELEMS || longest - shortest > 1u) {
        ok = 0;
    }

    printf("n=%" PRIu32 " cores=%" PRIu32 " imbalance=%" PRIu32
           " iteration(s)\n",
           (uint32_t)N_ELEMS, cl_core_num(), longest - shortest);
    puts(ok ? "PARALLEL FOR DEMO PASS" : "PARALLEL FOR DEMO FAIL");
    return ok ? 0 : 1;
}
