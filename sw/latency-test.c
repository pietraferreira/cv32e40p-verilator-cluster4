// SPDX-FileCopyrightText: 2024 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

/*
 * latency-test.c – measures cycle cost of scratchpad vs shared-memory accesses.
 *
 * Core 0 runs two tight loops (N iterations each):
 *   1. Read/write to its local scratchpad  (SPM, expected 1-cycle data latency)
 *   2. Read/write to a shared-memory array (configured for high latency in TB)
 *
 * It prints the cycle counts and asserts that scratchpad is strictly faster.
 * The test passes if SPM cycles < shared-memory cycles.
 */

#include <stdio.h>
#include <stdint.h>
#include "cluster_sync.h"

#define ITERS 200

/* Place a small array in shared memory (.bss) for the shared-memory test. */
static volatile uint32_t shared_buf[ITERS];

int main(void) {
    uint32_t hart = cl_read_mhartid();

    /* Only core 0 runs the benchmark; others exit immediately. */
    if (hart != 0u)
        return 0;

    volatile uint32_t *spm = SPM_PTR(hart);

    /* ---- warm-up: touch both regions once ---- */
    spm[0] = 0u;
    shared_buf[0] = 0u;
    cl_fence();

    /* ---- scratchpad loop ---- */
    uint32_t t0 = cl_read_mcycle();
    for (uint32_t i = 0; i < ITERS; i++) {
        spm[i] = i;
        uint32_t v = spm[i];
        (void)v;
    }
    cl_fence();
    uint32_t t1 = cl_read_mcycle();

    /* ---- shared-memory loop ---- */
    uint32_t t2 = cl_read_mcycle();
    for (uint32_t i = 0; i < ITERS; i++) {
        shared_buf[i] = i;
        uint32_t v = shared_buf[i];
        (void)v;
    }
    cl_fence();
    uint32_t t3 = cl_read_mcycle();

    uint32_t spm_cycles    = t1 - t0;
    uint32_t shared_cycles = t3 - t2;

    printf("LATENCY TEST: spm_cycles=%lu shared_cycles=%lu\n",
           (unsigned long)spm_cycles, (unsigned long)shared_cycles);

    if (spm_cycles < shared_cycles) {
        puts("LATENCY TEST PASS");
        return 0;
    } else {
        puts("LATENCY TEST FAIL");
        return 1;
    }
}
