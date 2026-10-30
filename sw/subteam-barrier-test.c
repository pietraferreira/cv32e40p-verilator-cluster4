// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-FileCopyrightText: 2026 Embecosm Limited
// SPDX-License-Identifier: Apache-2.0

/*
 * subteam-barrier-test.c - a barrier over cores 0 and 1 only.
 *
 * The event unit only has one barrier, but it respects the core mask, so
 * two cores can meet while the other two carry on. Core 1 is made to
 * arrive late and core 0 should wait for it. Then all four cores meet on
 * a full barrier.
 *
 *   make APP=subteam-barrier-test run
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cluster_sync.h"

static volatile uint32_t sub_ready;
static volatile uint32_t full_ready;
static volatile uint32_t passed_sub[NUM_CORES];
static volatile uint32_t arrive_at[NUM_CORES];
static volatile uint32_t held_for[NUM_CORES];
static volatile uint32_t sink;

static void barrier(void) {
    cl_fence();
    cl_barrier_notify(0u);
    cl_evt_wait();
    cl_gpevt_clear(0u);
    cl_fence();
}

static void publish(volatile uint32_t *flag) {
    cl_fence();
    *flag = 1u;
    cl_fence();
}

int main(void) {
    uint32_t core = cl_read_mhartid();

    if (core == 0u) {
        cl_barrier_setup(0u, 2u, 0x3u); /* only the mask is used */
        publish(&sub_ready);
    }
    while (sub_ready == 0u) {
    }

    if (core < 2u) {
        if (core == 1u) {
            uint32_t acc = 1u;
            for (uint32_t i = 0u; i < 20000u; i++) {
                acc = acc * 1664525u + 1013904223u;
            }
            sink = acc;
        }
        // TICKS, because mcycle stops while the core sleeps.
        arrive_at[core] = cl_read_ticks();
        barrier();
        held_for[core] = cl_read_ticks() - arrive_at[core];
        passed_sub[core] = 1u;

        if (core == 0u) {
            cl_barrier_setup(0u, NUM_CORES, (1u << NUM_CORES) - 1u);
            publish(&full_ready);
        }
    }

    while (full_ready == 0u) {
    }
    barrier();

    if (core != 0u) {
        return 0;
    }

    uint32_t gap = arrive_at[1] - arrive_at[0];
    printf("core 1 arrived %" PRIu32 " cycles after core 0\n", gap);
    printf("core 0 was held for %" PRIu32 " cycles, core 1 for %" PRIu32
           "\n",
           held_for[0], held_for[1]);

    int ok = passed_sub[0] && passed_sub[1] && !passed_sub[2] &&
             !passed_sub[3] && arrive_at[1] > arrive_at[0] &&
             held_for[0] >= gap;
    puts(ok ? "SUBTEAM BARRIER TEST PASS" : "SUBTEAM BARRIER TEST FAIL");
    return ok ? 0 : 1;
}
