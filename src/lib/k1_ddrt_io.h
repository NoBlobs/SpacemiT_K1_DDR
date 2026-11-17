/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
#ifndef K1_DDRT_IO_H
#define K1_DDRT_IO_H

#include <stdint.h>

#ifdef K1_DDRT_MOCK

uint32_t k1_mock_read32(uintptr_t addr);
void k1_mock_write32(uintptr_t addr, uint32_t val);

static inline uint32_t k1_r32(uintptr_t addr)
{
	return k1_mock_read32(addr);
}

static inline void k1_w32(uintptr_t addr, uint32_t val)
{
	k1_mock_write32(addr, val);
}

#else

static inline void k1_io_fence(void)
{
#if defined(__riscv)
	__asm__ volatile("fence iorw, iorw" ::: "memory");
#else
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/* fully ordered: training is latency-bound by the PI, never by MMIO */
static inline uint32_t k1_r32(uintptr_t addr)
{
	uint32_t v = *(volatile const uint32_t *)addr;

	k1_io_fence();
	return v;
}

static inline void k1_w32(uintptr_t addr, uint32_t val)
{
	k1_io_fence();
	*(volatile uint32_t *)addr = val;
	k1_io_fence();
}

#endif

static inline void k1_rmw32(uintptr_t addr, uint32_t keep, uint32_t set)
{
	k1_w32(addr, (k1_r32(addr) & keep) | set);
}

#endif /* K1_DDRT_IO_H */
