# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Thalles Campagnani
#
# make blob        open DDR firmware (ddr_fw.bin drop-in) for mainline SPL
# make test        algorithm tests against the PI model, rv64gc under qemu
# make host-test   same tests natively with ASan/UBSan
# make host-test-exp  each K1_EXP_* error path against the model
# make check-tables diff the silicon tables against the RST spec in SPEC_DOCS
# make mutate      kill mutants of src/ against `make test`
#
# GCC only: riscv64-elf-gcc (blob), riscv64-linux-gnu-gcc (tests), gcc (host)
# Test sources, the PI model and the scripts live in $(TOOLS)/test.

BUILD     ?= ../build
TOOLS     ?= ../tools
SPEC_DOCS ?=
CROSS_ELF ?= riscv64-elf-
CROSS_LNX ?= riscv64-linux-gnu-
QEMU      ?= qemu-riscv64
HOSTCC    ?= gcc
# Independent eye is the production data-eye path (PI: bring-up + DQS gate).
BLOB_DEFS ?= -DK1_INDEP_EYE

STD  := -std=c23
ARCH := -march=rv64gc -mabi=lp64d -mcmodel=medany
WARN := -Wall -Wextra -Werror -Wshadow -Wundef -Wstrict-prototypes \
	-Wmissing-prototypes -Wpointer-arith -Wvla -Wformat=2
INC  := -Iinclude -Ilib

LIB_SRC  := lib/k1_ddrt.c lib/k1_ddr_boot.c lib/k1_ddr_init.c \
	lib/k1_ddr_tables.c lib/k1_ddr_accept.c lib/k1_ddr_eye.c
BLOB_SRC := blob/start.S blob/blob.c blob/fmt.c
TEST_DIR := $(TOOLS)/test
TEST_SRC := $(TEST_DIR)/model.c $(TEST_DIR)/test_main.c $(TEST_DIR)/test_boot.c \
	$(TEST_DIR)/test_accept.c $(TEST_DIR)/test_eye.c
HDRS     := $(wildcard include/*.h lib/*.h blob/*.h $(TEST_DIR)/*.h)

BLOB_CFLAGS := $(STD) $(ARCH) $(WARN) $(INC) -Os -ffreestanding \
	-fno-builtin -fno-pic -fno-pie -mno-relax -fno-stack-protector \
	-fno-asynchronous-unwind-tables -fno-unwind-tables \
	-ffunction-sections -fdata-sections -fno-common $(BLOB_DEFS)
BLOB_LDFLAGS := -nostdlib -static -no-pie -Wl,-T,blob/blob.ld \
	-Wl,--gc-sections -Wl,--no-relax -Wl,--build-id=none \
	-Wl,-Map,$(BUILD)/blob/k1_ddr_fw.map

TEST_CFLAGS := $(STD) $(WARN) $(INC) -O2 -g -DK1_DDRT_MOCK

BLOB_ELF := $(BUILD)/blob/k1_ddr_fw.elf
BLOB_BIN := $(BUILD)/blob/k1_ddr_fw.bin
TEST_RV  := $(BUILD)/test/k1_ddrt_test.rv64
TEST_HOST := $(BUILD)/test/k1_ddrt_test.host
DUMP_HOST := $(BUILD)/test/dump_tables

EXP_HOOKS := K1_EXP_PLL_NO_LOCK K1_EXP_GATE_TIMEOUT K1_EXP_DFC_TIMEOUT

.PHONY: all blob test host-test host-test-exp check-tables mutate clean

all: blob test

blob: $(BLOB_BIN)

$(BLOB_ELF): $(LIB_SRC) $(BLOB_SRC) blob/blob.ld $(HDRS)
	@mkdir -p $(@D)
	$(CROSS_ELF)gcc $(BLOB_CFLAGS) $(BLOB_LDFLAGS) -o $@ $(BLOB_SRC) $(LIB_SRC) -lgcc
	@if $(CROSS_ELF)nm -u $@ | grep -q .; then \
		echo "unresolved symbols in blob:"; $(CROSS_ELF)nm -u $@; exit 1; fi

$(BLOB_BIN): $(BLOB_ELF)
	$(CROSS_ELF)objcopy -O binary $< $@
	$(CROSS_ELF)size $<
	@echo "  $@: $$(stat -c %s $@) bytes"

$(TEST_RV): $(LIB_SRC) $(TEST_SRC) $(HDRS)
	@mkdir -p $(@D)
	$(CROSS_LNX)gcc $(TEST_CFLAGS) -march=rv64gc -mabi=lp64d -static -o $@ $(LIB_SRC) $(TEST_SRC)

test: $(TEST_RV)
	$(QEMU) $<

$(TEST_HOST): $(LIB_SRC) $(TEST_SRC) $(HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) $(TEST_CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=all -o $@ $(LIB_SRC) $(TEST_SRC)

host-test: $(TEST_HOST)
	$<

# one binary per injection flag: the production suite must stay EXP-free
host-test-exp:
	@mkdir -p $(BUILD)/test
	@for d in $(EXP_HOOKS); do \
		bin=$(BUILD)/test/exp-$$d.host; \
		echo "  $$d"; \
		$(HOSTCC) $(TEST_CFLAGS) -fsanitize=address,undefined \
			-fno-sanitize-recover=all -D$$d -o $$bin \
			$(LIB_SRC) $(TEST_DIR)/model.c $(TEST_DIR)/test_exp.c || exit 1; \
		$$bin || exit 1; \
	done

$(DUMP_HOST): $(LIB_SRC) $(TEST_DIR)/model.c $(TEST_DIR)/dump_tables.c $(HDRS)
	@mkdir -p $(@D)
	$(HOSTCC) $(TEST_CFLAGS) -o $@ $(LIB_SRC) $(TEST_DIR)/model.c $(TEST_DIR)/dump_tables.c

# SPEC_DOCS: directory with the original RST spec (25-pmu.rst ... 31-configuracoes.rst)
check-tables: $(DUMP_HOST)
	@test -n "$(SPEC_DOCS)" || { echo "check-tables: set SPEC_DOCS=<dir with the RST spec>"; exit 1; }
	python3 $(TEST_DIR)/check_tables.py $< "$(SPEC_DOCS)"

mutate:
	python3 $(TEST_DIR)/mutate.py

clean:
	rm -rf $(BUILD)/blob $(BUILD)/test
