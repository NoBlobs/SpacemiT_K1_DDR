/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
/* Helpers shared by the training pass and the controller bring-up. */
#ifndef K1_DDRT_PRIV_H
#define K1_DDRT_PRIV_H

#include "k1_ddrt.h"

[[gnu::format(printf, 2, 3)]]
void k1_log(const struct k1_ddrt_env *env, const char *fmt, ...);

/* 0 without env->now_us */
uint64_t k1_now(const struct k1_ddrt_env *env);

/* poll until (reg & mask) == want; budget needs now_us, the poll cap does not */
int k1_wait_n(const struct k1_ddrt_env *env, uintptr_t addr, uint32_t mask,
	      uint32_t want, uint32_t budget_us, uint32_t *last,
	      uint32_t *polls);
int k1_wait(const struct k1_ddrt_env *env, uintptr_t addr, uint32_t mask,
	    uint32_t want, uint32_t budget_us, uint32_t *last);

/* busy wait on now_us; a no-op without it */
void k1_udelay(const struct k1_ddrt_env *env, uint32_t us);

#endif /* K1_DDRT_PRIV_H */
