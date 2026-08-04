/* SPDX-License-Identifier: Apache-2.0 */

/*
 * Hart 0 alone runs bss-clear/__libc_init_array, so harts 1..N-1 must not enter 
   main() until that's done.
 */

#include <stdint.h>

static volatile uint32_t g_crt0_ready = 0u;

void crt0_hart0_signal_ready(void) {
  __asm__ volatile("fence rw, rw" ::: "memory");
  g_crt0_ready = 1u;
  __asm__ volatile("fence rw, rw" ::: "memory");
}

void crt0_wait_ready(void) {
  while (g_crt0_ready == 0u) {
  }
  __asm__ volatile("fence rw, rw" ::: "memory");
}
