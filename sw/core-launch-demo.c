// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

/*
 * Nothing starts the cores: they are already running. All four harts come
 * out of reset at the same address, run the same crt0, and enter main()
 * together. The entire multi-core model follows from that one fact, and
 * this demo shows its four consequences:
 *
 *   1. Identity - each hart finds out who it is from mhartid.
 *   2. Primary-only work - initialisation, printing etc belong to hart 0.
 *   3. Sync - the barrier is what turns four independent programs into one 
        parallel program.
 *   4. Join before return - hart 0 returning from main() writes the EXIT
 *      register and kills the simulation for everyone.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "cluster_team.h"

static volatile uint32_t reported_id[NUM_CORES];
static volatile uint32_t reported_sp[NUM_CORES];
static volatile uint32_t saw_init;
static cl_team_t team;

static uint32_t read_sp(void) {
    uint32_t sp;
    __asm__ volatile("mv %0, sp" : "=r"(sp));
    return sp;
}

int main(void) {
    uint32_t hart = cl_core_idx();

    if (hart >= cl_core_num()) {
        return 1;
    }

    if (cl_is_primary()) {
        cl_team_init(&team);
        for (uint32_t i = 0u; i < cl_core_num(); i++) {
            reported_id[i] = 0xFFFFFFFFu;
            reported_sp[i] = 0u;
        }
        saw_init = 1u;
        cl_fence();
    }

    // Sync
    cl_team_barrier(&team);

    reported_id[hart] = hart;
    reported_sp[hart] = read_sp();
    cl_fence();

    cl_team_barrier(&team);

    if (!cl_is_primary()) {
        return 0;
    }

    uint32_t fail = (saw_init == 1u) ? 0u : 1u;

    printf("cores=%" PRIu32 "\n", cl_core_num());
    for (uint32_t i = 0u; i < cl_core_num(); i++) {
        printf("  hart %" PRIu32 ": mhartid=%" PRIu32 " sp=0x%08" PRIx32 "\n",
               i, reported_id[i], reported_sp[i]);
        if (reported_id[i] != i) {
            fail = 1u;
        }
    }

    if (cl_core_num() > 1u) {
        uint32_t stride = reported_sp[0] - reported_sp[1];
        printf("stack stride=%" PRIu32 " bytes/hart\n", stride);
    }

    if (fail == 0u) {
        puts("CORE LAUNCH DEMO PASS");
    } else {
        puts("CORE LAUNCH DEMO FAIL");
    }
    return (int)fail;
}
