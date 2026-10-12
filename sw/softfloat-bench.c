// SPDX-FileCopyrightText: 2026 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

// Cost of single-precision float

#include <stdint.h>
#include <stdio.h>

#include "cluster_sync.h"

#define ITERS 1000u
#define NVALS 16u

static volatile float fsrc[NVALS] = {
    1.5f, 3.25f, -2.75f, 7.125f, 0.3f, 12.5f, -0.6f, 9.75f,
    2.2f, -5.5f, 0.125f, 31.0f, -1.1f, 4.4f, 6.6f, -8.8f,
};
static volatile int32_t isrc[NVALS] = {
    3, 7, -2, 11, 5, 13, -4, 9, 2, -6, 1, 31, -1, 4, 6, -8,
};
static volatile float fsink;
static volatile int32_t isink;

typedef struct {
    uint32_t ticks;
    uint32_t instret;
} cost_t;

#define MEASURE(out, body)                                  \
    do {                                                    \
        uint32_t t0 = cl_read_ticks();                      \
        uint32_t i0 = cl_read_minstret();                   \
        for (uint32_t i = 0; i < ITERS; i++) {              \
            body;                                           \
        }                                                   \
        (out).instret = cl_read_minstret() - i0;            \
        (out).ticks = cl_read_ticks() - t0;                 \
    } while (0)

static void report(const char *name, cost_t op, cost_t base) {
    printf("%-14s %6lu cycles %5lu instructions per op\n", name,
           (unsigned long)((op.ticks - base.ticks) / ITERS),
           (unsigned long)((op.instret - base.instret) / ITERS));
}

int main(void) {
    if (cl_read_mhartid() != 0)
        return 0;

    cost_t base, add, mul, div, cvt;
    MEASURE(base, isink = isrc[i % NVALS] + isrc[(i + 1) % NVALS]);
    MEASURE(add, fsink = fsrc[i % NVALS] + fsrc[(i + 1) % NVALS]);
    MEASURE(mul, fsink = fsrc[i % NVALS] * fsrc[(i + 1) % NVALS]);
    MEASURE(div, fsink = fsrc[i % NVALS] / fsrc[(i + 1) % NVALS]);
    MEASURE(cvt, fsink = (float)(isrc[i % NVALS] + isrc[(i + 1) % NVALS]));

    printf("baseline (int add loop): %lu cycles per iteration\n",
           (unsigned long)(base.ticks / ITERS));
    report("float add", add, base);
    report("float multiply", mul, base);
    report("float divide", div, base);
    report("int32 to float", cvt, base);
    puts("SOFTFLOAT BENCH DONE");
    return 0;
}
