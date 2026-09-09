/* Copyright (c) 2017  SiFive Inc. All rights reserved.
 * Copyright (c) 2019  ETH Zürich and University of Bologna
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the FreeBSD License.   This program is distributed in the hope that
 * it will be useful, but WITHOUT ANY WARRANTY expressed or implied,
 * including the implied warranties of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  A copy of this license is available at
 * http://www.opensource.org/licenses.
 */
/* Make sure the vector table gets linked into the binary.  */
.global vector_table

/* Entry point for bare metal programs */
.section .text.start
.global _start
.type _start, @function

_start:
/* initialize global pointer */
.option push
.option norelax
1:	auipc gp, %pcrel_hi(__global_pointer$)
	addi  gp, gp, %pcrel_lo(1b)
.option pop

/* initialize stack pointer */
	la sp, __stack_end
	csrr t0, mhartid
	li t1, 0x4000	/* 16KiB per hart -- 1KiB overflows into the next hart's stack */
	mul t0, t0, t1
	sub sp, sp, t0

/* set vector table address */
	la a0, __vector_start
	ori a0, a0, 1 /*vector mode = vectored */
	csrw mtvec, a0

/* Only hart 0 runs the bss-clear and libc init; the other harts spin-wait until 
it signals completion, then all harts call main. */
	csrr t0, mhartid
	bnez t0, .Lhart_wait_init

/* clear the bss segment */
	la a0, _edata
	la a2, _end
	sub a2, a2, a0
	li a1, 0
	call memset

/* new-style constructors and destructors */
	la a0, __libc_fini_array
	call atexit
	call __libc_init_array

	call crt0_hart0_signal_ready
	j .Lenter_main

.Lhart_wait_init:
	call crt0_wait_ready

.Lenter_main:
/* call main */
//	lw a0, 0(sp)                    /* a0 = argc */
//	addi a1, sp, __SIZEOF_POINTER__ /* a1 = argv */
//	li a2, 0                        /* a2 = envp = NULL */
// Initialize these variables to 0. Cannot use argc or argv
// since the stack is not initialized
	li a0, 0
	li a1, 0
	li a2, 0

	call main

/* only hart 0 tears down the sim; others park (mirrors _exit's own
 * hart-0-only EXIT_REG write in syscalls.c) */
	csrr t0, mhartid
	bnez t0, .Lhart_park

	tail exit

.Lhart_park:
	wfi
	j .Lhart_park

.size  _start, .-_start

.global _init
.type   _init, @function
.global _fini
.type   _fini, @function
_init:
_fini:
 /* These don't have to do anything since we use init_array/fini_array. Prevent
    missing symbol error */
	ret
.size  _init, .-_init
.size _fini, .-_fini
