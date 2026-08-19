#include "cluster_sync.h"
#include <stdint.h>
#include <stdio.h>

#define BYTES 512u
#define WORDS (BYTES / 4u)

// This should land in bss (shared memory), so we can compare with "slow memory"
static volatile uint32_t src_buf[WORDS];

typedef struct {
    uint32_t cycles;
    uint32_t instrs;
} meas_t;

static inline meas_t meas_start(void) {
    meas_t m;
    m.cycles = cl_read_mcycle();
    m.instrs = cl_read_minstret();
    return m;
}

static inline meas_t meas_end(meas_t start) {
    meas_t d;
    d.cycles = cl_read_mcycle() - start.cycles;
    d.instrs = cl_read_minstret() - start.instrs;
    return d;
}

static void report_header(const char *unit_name) {
    printf("\n| %-25s | %8s | %8s | %7s | cyc/%-4s |\n", "bench", "mcycle",
           "minstret", "stall", unit_name);
    printf("| %-25s | %8s | %8s | %7s | %8s |\n", ":------------------------",
           "-------:", "-------:", "------:", "-------:");
}

// Stall cycles = cycles the core spent not retiring anything.
static void report(const char *name, meas_t m, uint32_t units) {
    uint32_t stall = (m.cycles > m.instrs) ? (m.cycles - m.instrs) : 0u;
    printf("| %-25s | %8lu | %8lu | %7lu | %5lu.%02lu |\n", name,
           (unsigned long)m.cycles, (unsigned long)m.instrs,
           (unsigned long)stall, (unsigned long)(m.cycles / units),
           (unsigned long)((m.cycles * 100u / units) % 100u));
}

static uint32_t __attribute__((noinline)) read_loop(volatile const uint32_t *p) {
    uint32_t a = 0u;
    for (uint32_t i = 0; i < WORDS; i++)
        a += p[i];
    return a;
}

static void __attribute__((noinline))
copy_words(volatile uint32_t *dst, volatile const uint32_t *src) {
    for (uint32_t i = 0; i < WORDS; i++)
        *dst++ = *src++;
}

static void __attribute__((noinline))
copy_bytes(volatile uint8_t *dst, volatile const uint8_t *src) {
    for (uint32_t i = 0; i < BYTES; i++)
        *dst++ = *src++;
}

static void clear_dst(volatile uint32_t *dst) {
    for (uint32_t i = 0; i < WORDS; i++) {
        dst[i] = 0u;
    }
    cl_fence();
}

static int verify(volatile uint32_t *dst, const char *what) {
    for (uint32_t i = 0; i < WORDS; i++) {
        if (dst[i] != src_buf[i]) {
            printf("DMA BENCH FAIL (%s) at word %lu\n", what, (unsigned long)i);
            return 0;
        }
    }
    return 1;
}

int main(void) {
    // crt0 runs main() on all 4 harts. Everything below is single-core
    if (cl_read_mhartid() != 0u) {
        return 0;
    }

    volatile uint32_t *spm = SPM_PTR(0); // this core's own scratchpad

    for (uint32_t i = 0; i < WORDS; i++) {
        src_buf[i] = 0xA5000000u ^ i;
    }
    cl_fence();

    printf("DMA BENCH: %lu-byte copy\n", (unsigned long)BYTES);

    // DMA engine
    // We clear the destination scratchpad, measure, then verify byte-for-byte
    // against src_buf before trusting the number
    clear_dst(spm);
    meas_t s = meas_start();
    // src, dst, len
    cl_dma_memcpy((void *)spm, (const void *)src_buf, BYTES);
    // stall until idle
    cl_dma_wait();
    meas_t m_dma = meas_end(s);
    if (!verify(spm, "dma"))
        return 1;

    // Core word copy
    clear_dst(spm);
    s = meas_start();
    copy_words(spm, src_buf);
    cl_fence();
    meas_t m_word = meas_end(s);
    if (!verify(spm, "word"))
        return 1;

    // Core byte copy to compare
    volatile uint8_t *spm_b = (volatile uint8_t *)spm;
    volatile const uint8_t *src_b = (volatile const uint8_t *)src_buf;
    clear_dst(spm);
    s = meas_start();
    copy_bytes(spm_b, src_b);
    cl_fence();
    meas_t m_byte = meas_end(s);
    if (!verify(spm, "byte"))
        return 1;

    report_header("byte");
    report("dma", m_dma, BYTES);
    report("cpu-word", m_word, BYTES);
    report("cpu-byte", m_byte, BYTES);

    printf("\nREAD BENCH: %lu-word read loop\n", (unsigned long)WORDS);

    volatile uint32_t *rem = SPM_PTR(1); // core 1's scratchpad
    uint32_t acc = 0u;

    // Same pattern as before, but read-and-accumulate loop
    // Using core 0's (this core) scratchpad
    s = meas_start();
    acc += read_loop(spm);
    meas_t m_local = meas_end(s);

    // Using a different core scratchpad (core 1)
    s = meas_start();
    acc += read_loop(rem);
    meas_t m_remote = meas_end(s);

    // Using shared (slow) memory
    s = meas_start();
    acc += read_loop(src_buf);
    meas_t m_shared = meas_end(s);

    report_header("word");
    report("local-scratch (close)", m_local, WORDS);
    report("remote-scratch (core 1's)", m_remote, WORDS);
    report("shared (far)", m_shared, WORDS);

    //printf("  (acc=%lu)\n", (unsigned long)acc);
    puts("DMA BENCH PASS");
    return 0;
}
