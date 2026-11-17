/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (C) 2026 Thalles Campagnani */
#ifndef K1_BLOB_FMT_H
#define K1_BLOB_FMT_H

#include <stdarg.h>
#include <stddef.h>

/* subset of vsnprintf: %% %c %s %d %i %u %x %X %p, flags '-' '0', width, l ll z h hh */
int k1_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

#endif /* K1_BLOB_FMT_H */
