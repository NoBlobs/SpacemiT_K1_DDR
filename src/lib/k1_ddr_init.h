/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/* Building blocks of the K1 DDR bring-up, sequenced by k1_ddr_boot(). */
#ifndef K1_DDR_INIT_H
#define K1_DDR_INIT_H

#include "k1_ddr_boot.h"
#include "k1_ddr_tables.h"

struct k1_ddr_hw {
	uintptr_t ddr;
	uintptr_t phy;
	uintptr_t pmu;
	uintptr_t win;		/* DFC table window, MC offsets apply */
	uint32_t cs_num;
	uint32_t cs_sel;	/* bit 0 CS0, bit 1 CS1 */
	enum k1_ddr_type type;
	const struct k1_io_cfg *io;
	const struct k1_ddrt_env *env;
};

/* one CS, from MR8 (doc 29) */
struct k1_ddr_geom {
	uint32_t area_mb;
	uint32_t code;		/* CS map [20:16] */
	uint32_t row;		/* ROW_n */
	bool x8;
};

struct k1_ddr_hw k1_ddr_hw_init(const struct k1_ddr_boot_cfg *cfg,
				const struct k1_ddrt_env *env);

/* PLL1/PLL2, lock, SW-FC to fp0, MC clock (doc 25) */
[[nodiscard]] int k1_ddr_pll_init(const struct k1_ddr_hw *hw,
				  uint32_t data_rate, enum k1_ddr_step *step);

/* MC banks, PHY analog, DFI init, JEDEC init, MR, ZQCAL at fp0 (doc 26..28) */
[[nodiscard]] int k1_ddr_device_init(const struct k1_ddr_hw *hw, bool x8,
				     enum k1_ddr_step *step);

/* PMU+0xb0 bit 21 pulse; PLLs stay locked (doc 25, 31) */
void k1_ddr_mc_reset(const struct k1_ddr_hw *hw);

/* CS map size code of one CS, 0 if the size has none */
uint32_t k1_ddr_size_code(uint32_t mb);
[[nodiscard]] int k1_ddr_geometry(uint8_t mr8, struct k1_ddr_geom *g);
void k1_ddr_map_apply(const struct k1_ddr_hw *hw, const struct k1_ddr_geom *g);

/* command flow, timing snapshot and analog markers through the window (doc 30) */
void k1_ddr_dfc_table_load(const struct k1_ddr_hw *hw, bool mr21);

/*
 * Hardware DFC to fp (doc 30). Refused while a rank is in self-refresh. On
 * a timeout the ranks are taken out of self-refresh; kick bit 22 may stay
 * set, and then no DFC may be kicked before an MC reset.
 */
[[nodiscard]] int k1_ddr_dfc(const struct k1_ddr_hw *hw, uint32_t fp);

/* MR write from the active bank; mr is the low byte of the command */
void k1_ddr_mr_write(const struct k1_ddr_hw *hw, uint32_t mr);

#endif /* K1_DDR_INIT_H */
