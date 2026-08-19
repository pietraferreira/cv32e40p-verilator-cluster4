// SPDX-License-Identifier: Apache-2.0
//
// Exercises the DMA across every combination of source/destination byte
// alignment and a spread of lengths, including ones shorter than a word.
//
// The engine moves a full word per read/write pair only when both ends are
// 4-byte aligned and a whole word is left; everything else falls back to one
// byte at a time. That makes the head and tail of an unaligned transfer take a
// different path from the middle, so each case checks two things:
//
//   - every byte in the requested range arrived, and
//   - no byte outside it moved. A word-granular write touches 4 bytes at once,
//     so a wrong byte-enable or a wrong end condition shows up as a clobbered
//     guard byte next to the range rather than as missing data.

#include "cluster_sync.h"
#include <stdint.h>
#include <stdio.h>

#define SRC_BYTES 64u
#define DST_BYTES 96u
#define GUARD     0x5Au

// Uninitialised global lands in .bss, which lives in shared memory
static volatile uint8_t src[SRC_BYTES];

static uint32_t cases_run = 0u;
static uint32_t failures = 0u;

static void run_case(volatile uint8_t *dst, uint32_t src_off, uint32_t dst_off,
                     uint32_t len) {
    for (uint32_t i = 0; i < DST_BYTES; i++)
        dst[i] = GUARD;
    cl_fence();

    cl_dma_memcpy((void *)(dst + dst_off), (const void *)(src + src_off), len);
    cl_dma_wait();

    cases_run++;

    for (uint32_t i = 0; i < DST_BYTES; i++) {
        uint8_t want = (i >= dst_off && i < dst_off + len)
                           ? src[src_off + (i - dst_off)]
                           : (uint8_t)GUARD;
        uint8_t got = dst[i];
        if (got != want) {
            printf("FAIL src_off=%lu dst_off=%lu len=%2lu : byte %2lu is %02x, "
                   "expected %02x%s\n",
                   (unsigned long)src_off, (unsigned long)dst_off,
                   (unsigned long)len, (unsigned long)i, (unsigned)got,
                   (unsigned)want,
                   (i < dst_off || i >= dst_off + len) ? " (guard clobbered)"
                                                       : "");
            failures++;
            return;
        }
    }
}

int main(void) {
    if (cl_read_mhartid() != 0u)
        return 0;

    volatile uint8_t *dst = (volatile uint8_t *)SPM_PTR(0);

    for (uint32_t i = 0; i < SRC_BYTES; i++)
        src[i] = (uint8_t)(0xC0u + i);
    cl_fence();

    static const uint32_t lens[] = {1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 15u, 16u, 17u, 32u};

    puts("DMA ALIGN TEST");

    for (uint32_t s = 0u; s < 4u; s++)
        for (uint32_t d = 0u; d < 4u; d++)
            for (uint32_t l = 0u; l < sizeof(lens) / sizeof(lens[0]); l++)
                run_case(dst, s, d, lens[l]);

    printf("  %lu cases, %lu failures\n", (unsigned long)cases_run,
           (unsigned long)failures);

    if (failures) {
        puts("DMA ALIGN TEST FAIL");
        return 1;
    }
    puts("DMA ALIGN TEST PASS");
    return 0;
}
