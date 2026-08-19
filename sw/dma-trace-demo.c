// SPDX-License-Identifier: Apache-2.0
//
// A deliberately tiny DMA copy, sized so that every bus transaction it causes
// fits on one screen. Run it with +dma_trace to see them:
//
//   make TOOLCHAIN_ROOT=<...> APP=dma-trace-demo verilate firmware
//   build/verilator/obj_dir/Vtb_top_verilator \
//       +firmware=build/fw/dma-trace-demo.hex +maxcycles=2000000 +dma_trace

#include "cluster_sync.h"
#include <stdint.h>
#include <stdio.h>

#define BYTES 8u

// Uninitialised global lands in .bss, which lives in shared memory
static volatile uint8_t source[BYTES];

int main(void) {
    if (cl_read_mhartid() != 0u)
        return 0;

    volatile uint8_t *scratch = (volatile uint8_t *)SPM_PTR(0);

    for (uint32_t i = 0; i < BYTES; i++)
        source[i] = (uint8_t)(0x11u * (i + 1u));
    for (uint32_t i = 0; i < BYTES; i++)
        scratch[i] = 0u;
    cl_fence();

    printf("copying %lu bytes from %08lx to %08lx\n", (unsigned long)BYTES,
           (unsigned long)(uintptr_t)source, (unsigned long)(uintptr_t)scratch);

    cl_dma_memcpy((void *)scratch, (const void *)source, BYTES);
    cl_dma_wait();

    for (uint32_t i = 0; i < BYTES; i++) {
        if (scratch[i] != source[i]) {
            printf("DMA TRACE DEMO FAIL at byte %lu\n", (unsigned long)i);
            return 1;
        }
    }
    puts("DMA TRACE DEMO PASS");
    return 0;
}
