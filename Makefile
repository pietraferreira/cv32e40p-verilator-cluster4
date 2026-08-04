# SPDX-FileCopyrightText: 2024 emmtrix Technologies GmbH
# SPDX-License-Identifier: Apache-2.0

ROOT_DIR := $(abspath .)
CV32E40P_DIR ?= cv32e40p
RTL_DIR := $(abspath $(CV32E40P_DIR)/rtl)
MANIFEST := $(CV32E40P_DIR)/cv32e40p_manifest.flist

TOOLCHAIN_ROOT ?= /opt/corev
RISCV_PREFIX ?= riscv32-corev-elf-
CROSS := $(TOOLCHAIN_ROOT)/bin/$(RISCV_PREFIX)
CC := $(CROSS)gcc
AR := $(CROSS)ar
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
READELF := $(CROSS)readelf

VERILATOR ?= verilator

RISCV_ARCH ?= rv32imc_zicsr_zifencei
RISCV_ABI ?= ilp32
CFLAGS := -Os -g -Wall -pedantic -mabi=$(RISCV_ABI) -march=$(RISCV_ARCH)

BUILD_DIR := build
FW_BUILD_DIR := $(BUILD_DIR)/fw
VERI_BUILD_DIR := $(BUILD_DIR)/verilator
VERI_OBJ_DIR := $(VERI_BUILD_DIR)/obj_dir

APP ?= shared-memory-demo
FW_ELF := $(FW_BUILD_DIR)/$(APP).elf
FW_HEX := $(FW_BUILD_DIR)/$(APP).hex
FW_MAP := $(FW_BUILD_DIR)/$(APP).map

BSP_SRCS := bsp/crt0.S bsp/handlers.S bsp/vectors.S bsp/syscalls.c bsp/startup_sync.c
BSP_OBJS := $(addprefix $(FW_BUILD_DIR)/,$(notdir $(BSP_SRCS:.S=.o)))
BSP_OBJS := $(BSP_OBJS:.c=.o)
BSP_LIB := $(FW_BUILD_DIR)/libcv-verif.a

APP_SRC := sw/$(APP).c
APP_OBJ := $(FW_BUILD_DIR)/$(APP).o

SV_TB_SRCS := \
	tb/tb_top_verilator.sv \
	tb/tb_mem_types_pkg.sv \
	tb/tb_core_dma_engine.sv \
	tb/tb_cv32e40p_cluster_wrapper.sv \
	tb/tb_cv32e40p_cluster_core.sv \
	tb/rr_arbiter.sv \
	tb/tb_event_unit.sv \
	tb/tb_shared_latency_pipe.sv \
	tb/tb_riscv/include/perturbation_defines.sv \
	tb/tb_riscv/riscv_rvalid_stall.sv \
	tb/tb_riscv/riscv_gnt_stall.sv \
	tb/mm_ram.sv \
	tb/dp_ram.sv \
	tb/scratchpad_ram.sv

CPP_TB_SRC := $(abspath tb/tb_top_verilator.cpp)
SIM_EXE := $(VERI_OBJ_DIR)/Vtb_top_verilator

MAXCYCLES ?= 20000000
VERI_CFLAGS ?= -O2
VERI_TRACE ?=

.PHONY: all run firmware verilate clean

all: run

run: verilate firmware
	$(SIM_EXE) +firmware=$(FW_HEX) +maxcycles=$(MAXCYCLES)

firmware: $(FW_HEX)

$(FW_HEX): $(FW_ELF)
	$(OBJCOPY) -O verilog $< $@
	$(READELF) -a $< > $(FW_BUILD_DIR)/$(APP).readelf
	$(OBJDUMP) -d -M no-aliases -M numeric -S $< > $(FW_BUILD_DIR)/$(APP).objdump

$(FW_ELF): $(APP_OBJ) $(BSP_LIB) bsp/link.ld | $(FW_BUILD_DIR)
	$(CC) $(CFLAGS) -nostartfiles -o $@ \
		$(APP_OBJ) -T bsp/link.ld -L$(FW_BUILD_DIR) -lcv-verif \
		-Wl,-Map,$(FW_MAP) -lc -lgcc

$(BSP_LIB): $(BSP_OBJS)
	$(AR) rcs $@ $^

$(FW_BUILD_DIR)/%.o: bsp/%.S | $(FW_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(FW_BUILD_DIR)/%.o: bsp/%.c | $(FW_BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(APP_OBJ): $(APP_SRC) sw/cluster_sync.h | $(FW_BUILD_DIR)
	$(CC) $(CFLAGS) -I bsp -I sw -c $< -o $@

verilate: $(SIM_EXE)

$(SIM_EXE): $(SV_TB_SRCS) $(CPP_TB_SRC) $(MANIFEST) | $(VERI_BUILD_DIR)
	DESIGN_RTL_DIR=$(RTL_DIR) $(VERILATOR) --cc --sv --exe \
		--top-module tb_top_verilator \
		--Wno-lint --Wno-UNOPTFLAT --Wno-MODDUP \
		--Wno-BLKANDNBLK --Wno-MULTIDRIVEN --Wno-COMBDLY \
		$(VERI_TRACE) \
		$(SV_TB_SRCS) -f $(MANIFEST) $(CPP_TB_SRC) \
		--Mdir $(VERI_OBJ_DIR) \
		-CFLAGS "-std=gnu++14 $(VERI_CFLAGS)"
	$(MAKE) -C $(VERI_OBJ_DIR) -f Vtb_top_verilator.mk

$(FW_BUILD_DIR):
	mkdir -p $(FW_BUILD_DIR)

$(VERI_BUILD_DIR):
	mkdir -p $(VERI_BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
