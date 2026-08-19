// SPDX-License-Identifier: Apache-2.0
//
// The engine has four registers. You write three of them to describe a copy,
// and reading the fourth waits for it to finish:
//
//   0x1500_3000  SRC    where to copy from
//   0x1500_3004  DST    where to copy to
//   0x1500_3008  LEN    how many BYTES - writing this starts the copy
//   0x1500_300C  WAIT   reading this blocks until the copy is done
//
// SRC and DST must be written before LEN, because the write to LEN is what
// captures them and launches the transfer.

#include "cluster_sync.h"
#include <stdint.h>
#include <stdio.h>

#define WORDS 8u    // 8 words = 32 bytes
#define BYTES (WORDS * 4u)

// Uninitialised global lands in .bss, which lives in shared memory
static volatile uint32_t source[WORDS];

static int failed = 0;

// Print all 8 words on one line
static void show(const char *label, volatile uint32_t *p) {
    printf("    %-10s", label);
    for (uint32_t i = 0; i < WORDS; i++)
        printf(" %08lx", (unsigned long)p[i]);
    printf("\n");
}

static void expect_copy(volatile uint32_t *dst) {
    for (uint32_t i = 0; i < WORDS; i++) {
        if (dst[i] != source[i]) {
            printf("    -> WRONG at word %lu\n", (unsigned long)i);
            failed = 1;
            return;
        }
    }
    printf("    -> correct\n");
}

static void clear(volatile uint32_t *p) {
    for (uint32_t i = 0; i < WORDS; i++)
        p[i] = 0u;
}

int main(void) {
    if (cl_read_mhartid() != 0u)
        return 0;

    volatile uint32_t *scratch = SPM_PTR(0);

    for (uint32_t i = 0; i < WORDS; i++)
        source[i] = 0xAA00u + i;

    puts("DMA USAGE DEMO\n");
    show("source", source);

    puts("\n1. Driving the registers by hand");
    // Four accesses
    clear(scratch);

    cl_mmio_write(CL_DMA_SRC_ADDR, (uint32_t)(uintptr_t)source);   // from
    cl_mmio_write(CL_DMA_DST_ADDR, (uint32_t)(uintptr_t)scratch);  // to
    cl_mmio_write(CL_DMA_LEN_ADDR, BYTES);                         // go
    (void)cl_mmio_read(CL_DMA_WAIT_ADDR);                          // wait

    show("scratch", scratch);
    expect_copy(scratch);

    puts("\n2. The same copy, using the helpers");
    // cl_dma_memcpy() does exactly the three writes above, in that order
    // cl_dma_wait() does the read. The argument order is (dst, src, len),
    // like memcpy - the opposite of the register write order
    clear(scratch);

    cl_dma_memcpy((void *)scratch, (const void *)source, BYTES);
    cl_dma_wait();

    show("scratch", scratch);
    expect_copy(scratch);

    puts("\n3. The copy runs in the background");
    // cl_dma_memcpy() only writes three registers and returns - it does not
    // wait. All the time is spent in cl_dma_wait()
    clear(scratch);

    uint32_t t0 = cl_read_mcycle();
    cl_dma_memcpy((void *)scratch, (const void *)source, BYTES);
    uint32_t t1 = cl_read_mcycle();
    cl_dma_wait();
    uint32_t t2 = cl_read_mcycle();

    printf("    starting the copy took %lu cycles\n", (unsigned long)(t1 - t0));
    printf("    waiting for it took    %lu cycles\n", (unsigned long)(t2 - t1));
    expect_copy(scratch);

    if (failed) {
        puts("\nDMA USAGE DEMO FAIL");
        return 1;
    }
    puts("\nDMA USAGE DEMO PASS");
    return 0;
}
