/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/*
 * Silicon tables of the K1 DDR bring-up (docs 25, 26, 27, 30, 31).
 * Kept apart from the code so test/check_tables.py can diff them against
 * the documentation.
 */
#ifndef K1_DDR_TABLES_H
#define K1_DDR_TABLES_H

#include <stddef.h>
#include <stdint.h>

struct k1_reg_val {
	uint16_t off;
	uint32_t val;
};

/* one MC word per frequency point; io rows are computed from k1_io_cfg */
struct k1_bank_row {
	uint16_t off;
	bool io;
	uint32_t fp[4];
};

struct k1_tbl_entry {
	uint32_t data;
	uint32_t addr;		/* MC offset | flags */
	uint16_t idx;
};

/* resistance encodings 1=240 2=120 3=80 4=60 5=48 6=40 ohm */
struct k1_io_cfg {
	uint8_t ca_vref;	/* ddr+0x30c[31:24], MR12 */
	uint8_t tx_vref;	/* ddr+0x30c[23:16], MR14 seed */
	uint8_t soc_odt;	/* ddr+0x30c[15:12] */
	uint8_t tx_odt;		/* ddr+0x310/314[22:20] */
	uint8_t rx_drv;		/* ddr+0x310/314[19:16] */
	uint8_t tx_pdrv;	/* COMMON+0xc[15:13] */
	uint8_t tx_ndrv;	/* COMMON+0xc[12:10] */
	uint8_t rx_odt;		/* COMMON+0xc[21:16], twice */
	uint8_t rx_vref;	/* COMMON+0x4[31:16], twice */
	uint8_t rx_pu_cal;	/* ddr+0x2c8[17], VOH */
};

static constexpr size_t K1_N_MC_GLOBALS = 20;
static constexpr size_t K1_N_MC_TIMING  = 24;
static constexpr size_t K1_N_X8_OVERLAY = 2;
static constexpr size_t K1_N_DFC_FLOW   = 29;
static constexpr size_t K1_N_DFC_EXIT   = 15;
static constexpr size_t K1_N_SNAP_GLOBALS = 22;
static constexpr size_t K1_N_SNAP_BANK  = 25;
static constexpr size_t K1_N_SNAP_TAIL  = 15;
static constexpr size_t K1_N_MR_INIT1   = 5;
static constexpr size_t K1_N_MR_INIT2   = 4;
static constexpr size_t K1_N_DFC_MARKS  = 3;

/* where MR21 goes in each command sequence when the part is 16 GiB */
static constexpr uint16_t K1_DFC_FLOW_MR21_AFTER = 0x18;
static constexpr uint16_t K1_DFC_FLOW_RUN_END    = 0x1b;
static constexpr uint16_t K1_DFC_EXIT_MR21_AFTER = 0x18b;
static constexpr uint16_t K1_DFC_EXIT_RUN_END    = 0x18e;

extern const struct k1_reg_val k1_mc_globals[K1_N_MC_GLOBALS];
extern const struct k1_bank_row k1_mc_timing[K1_N_MC_TIMING];
extern const struct k1_bank_row k1_mc_x8_overlay[K1_N_X8_OVERLAY];
extern const uint32_t k1_mc_wdqs[4];
extern const struct k1_reg_val k1_mc_refresh[3];
extern const uint32_t k1_clksel[4];
extern const struct k1_io_cfg k1_io_lpddr4x;
extern const struct k1_io_cfg k1_io_lpddr4;
extern const uint8_t k1_mr_init1[K1_N_MR_INIT1];
extern const uint8_t k1_mr_init2[K1_N_MR_INIT2];
extern const struct k1_tbl_entry k1_dfc_flow[K1_N_DFC_FLOW];
extern const struct k1_tbl_entry k1_dfc_exit[K1_N_DFC_EXIT];
extern const uint16_t k1_dfc_snap_globals[K1_N_SNAP_GLOBALS];
extern const uint16_t k1_dfc_snap_bank[K1_N_SNAP_BANK];
extern const struct k1_tbl_entry k1_dfc_snap_tail[K1_N_SNAP_TAIL];
extern const uint32_t k1_dfc_marks[K1_N_DFC_MARKS][3];	/* +0x10104, +0x10108, +0x10100 */

#endif /* K1_DDR_TABLES_H */
