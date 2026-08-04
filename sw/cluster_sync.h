// SPDX-FileCopyrightText: 2024 emmtrix Technologies GmbH
// SPDX-License-Identifier: Apache-2.0

#ifndef CLUSTER_SYNC_H
#define CLUSTER_SYNC_H

#include <stdint.h>

#ifndef NUM_CORES
#define NUM_CORES 4
#endif

/*
 * Scratchpad memory (SPM) address helpers.
 * SPM_SIZE must match the RTL parameter 2**SPM_ADDR_WIDTH (default 256 KiB).
 */
#define SPM_BASE_ADDR  0x18000000u
#define SPM_SIZE       262144u
#define SPM_ADDR(core_id)  (SPM_BASE_ADDR + (uint32_t)(core_id) * SPM_SIZE)
#define SPM_PTR(core_id)   ((volatile uint32_t *)SPM_ADDR(core_id))

/*
 * Minimal EU-style barrier peripheral addresses implemented by tb/mm_ram.sv.
 * These mirror the SDK usage pattern without depending on pulp-sdk headers.
 */
#define CL_EU_BASE_ADDR           0x15002000u
#define CL_EU_WAIT_BARRIER_ADDR   (CL_EU_BASE_ADDR + 0x0000u)
#define CL_EU_SET_BARRIER_ADDR    (CL_EU_BASE_ADDR + 0x0040u)
#define CL_EU_GPEVT_CLEAR_ADDR    (CL_EU_BASE_ADDR + 0x0084u)

/*
 * Per-core DMA MMIO aliases.
 * Write order: SRC, DST, LEN. Writing LEN enqueues the memcpy task.
 */
#define CL_DMA_BASE_ADDR          0x15003000u
#define CL_DMA_SRC_ADDR           (CL_DMA_BASE_ADDR + 0x0000u)
#define CL_DMA_DST_ADDR           (CL_DMA_BASE_ADDR + 0x0004u)
#define CL_DMA_LEN_ADDR           (CL_DMA_BASE_ADDR + 0x0008u)
#define CL_DMA_WAIT_ADDR          (CL_DMA_BASE_ADDR + 0x000Cu)

static inline uint32_t cl_read_mhartid(void) {
    uint32_t hart;
    __asm__ volatile ("csrr %0, mhartid" : "=r"(hart));
    return hart;
}

static inline uint32_t cl_read_mcycle(void) {
    uint32_t cyc;
    __asm__ volatile ("csrr %0, mcycle" : "=r"(cyc));
    return cyc;
}

static inline void cl_fence(void) {
    __asm__ volatile ("fence rw, rw" ::: "memory");
}

static inline void cl_mmio_write(uint32_t addr, uint32_t value) {
    *(volatile uint32_t *)addr = value;
}

static inline uint32_t cl_mmio_read(uint32_t addr) {
    return *(volatile uint32_t *)addr;
}

typedef struct {
    volatile uint32_t barrier_id;
    volatile uint32_t initialized;
} cl_barrier_t;

static inline void cl_barrier_setup(uint32_t barrier_id, uint32_t num_threads, uint32_t trigger_mask) {
    uint32_t cfg = (num_threads << 16) | trigger_mask;
    cl_mmio_write(CL_EU_SET_BARRIER_ADDR + (barrier_id << 2), cfg);
}

static inline void cl_barrier_notify(uint32_t barrier_id) {
    cl_mmio_write(CL_EU_WAIT_BARRIER_ADDR, barrier_id);
}

static inline void cl_evt_wait(void) {
    /*
     * Set mie[30] (EU barrier fast IRQ) so the core's irq_wu_ctrl fires when
     * the EU asserts irq_i[30].  mstatus.MIE stays 0, so the interrupt is
     * never *taken* (no ISR jump), but WFI exits as soon as the bit is pending.
     * Clear mie[30] again before returning so we leave no stray enables behind.
     */
    uint32_t eu_irq_mask = (1u << 30u);
    __asm__ volatile (
        "csrs mie, %0\n\t"
        "wfi\n\t"
        "csrc mie, %0"
        : : "r"(eu_irq_mask) : "memory"
    );
}

static inline void cl_gpevt_clear(uint32_t barrier_id) {
    cl_mmio_write(CL_EU_GPEVT_CLEAR_ADDR, barrier_id);
}

static inline void cl_dma_memcpy(void *dst, const void *src, uint32_t len_bytes) {
    cl_mmio_write(CL_DMA_SRC_ADDR, (uint32_t)(uintptr_t)src);
    cl_mmio_write(CL_DMA_DST_ADDR, (uint32_t)(uintptr_t)dst);
    cl_mmio_write(CL_DMA_LEN_ADDR, len_bytes);
}

static inline void cl_dma_wait(void) {
    (void)cl_mmio_read(CL_DMA_WAIT_ADDR);
}

static inline void cl_barrier_init(cl_barrier_t *bar) {
    uint32_t all_cores_mask = (NUM_CORES >= 32u) ? 0xFFFFFFFFu : ((1u << NUM_CORES) - 1u);
    bar->barrier_id = 0u;
    cl_barrier_setup(bar->barrier_id, NUM_CORES, all_cores_mask);
    cl_fence();
    bar->initialized = 1u;
    cl_fence();
}

static inline void cl_barrier_wait(cl_barrier_t *bar) {
    while (bar->initialized == 0u) {}
    cl_fence();
    cl_barrier_notify(bar->barrier_id);
    cl_evt_wait();
    cl_gpevt_clear(bar->barrier_id);
    cl_fence();
}

#endif
