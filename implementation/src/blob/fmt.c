// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Thalles Campagnani
/*
 * Freestanding runtime for the DDR blob: a small vsnprintf and the
 * mem* functions GCC may emit for struct copies and zeroing.
 */
#include <stdint.h>

#include "fmt.h"

void *memset(void *dst, int c, size_t n);
void *memcpy(void *restrict dst, const void *restrict src, size_t n);
void *memmove(void *dst, const void *src, size_t n);

void *memset(void *dst, int c, size_t n)
{
	volatile unsigned char *d = dst;

	while (n--)
		*d++ = (unsigned char)c;
	return dst;
}

void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
	volatile unsigned char *d = dst;
	const unsigned char *s = src;

	while (n--)
		*d++ = *s++;
	return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
	volatile unsigned char *d = dst;
	const unsigned char *s = src;

	if (d < s) {
		while (n--)
			*d++ = *s++;
	} else {
		d += n;
		s += n;
		while (n--)
			*--d = *--s;
	}
	return dst;
}

struct k1_out {
	char *buf;
	size_t size;
	size_t len;
};

static void k1_putc(struct k1_out *o, char c)
{
	if (o->len + 1 < o->size)
		o->buf[o->len] = c;
	o->len++;
}

static void k1_put_field(struct k1_out *o, const char *s, size_t n,
			 unsigned width, bool left, char pad)
{
	size_t fill = width > n ? width - n : 0;

	if (!left)
		while (fill--)
			k1_putc(o, pad);
	while (n--)
		k1_putc(o, *s++);
	if (left)
		while (fill--)
			k1_putc(o, ' ');
}

int k1_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct k1_out o = { .buf = buf, .size = size };

	for (; *fmt; fmt++) {
		char tmp[24];
		unsigned width = 0;
		bool left = false, zero = false, neg = false;
		int lng = 0;
		unsigned base = 10;
		bool upper = false;
		uint64_t v;
		size_t n = 0;

		if (*fmt != '%') {
			k1_putc(&o, *fmt);
			continue;
		}
		fmt++;
		for (;; fmt++) {
			if (*fmt == '-')
				left = true;
			else if (*fmt == '0')
				zero = true;
			else
				break;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (unsigned)(*fmt++ - '0');
		for (;; fmt++) {
			if (*fmt == 'l' || *fmt == 'z')
				lng++;
			else if (*fmt != 'h')
				break;
		}

		switch (*fmt) {
		case '\0':
			fmt--;
			continue;
		case '%':
			k1_putc(&o, '%');
			continue;
		case 'c':
			tmp[0] = (char)va_arg(ap, int);
			k1_put_field(&o, tmp, 1, width, left, ' ');
			continue;
		case 's': {
			const char *s = va_arg(ap, const char *);

			if (!s)
				s = "(null)";
			while (s[n])
				n++;
			k1_put_field(&o, s, n, width, left, ' ');
			continue;
		}
		case 'd':
		case 'i': {
			const int64_t sv = lng ? va_arg(ap, int64_t) : va_arg(ap, int);

			neg = sv < 0;
			v = neg ? (uint64_t)0 - (uint64_t)sv : (uint64_t)sv;
			break;
		}
		case 'p':
			v = (uintptr_t)va_arg(ap, void *);
			base = 16;
			break;
		case 'X':
			upper = true;
			[[fallthrough]];
		case 'x':
			base = 16;
			[[fallthrough]];
		case 'u':
			v = lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned int);
			break;
		default:
			k1_putc(&o, '%');
			k1_putc(&o, *fmt);
			continue;
		}

		do {
			const unsigned d = (unsigned)(v % base);

			tmp[sizeof(tmp) - 1 - n++] =
				(char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
			v /= base;
		} while (v);
		if (neg && zero && !left) {
			k1_putc(&o, '-');
			if (width)
				width--;
		} else if (neg) {
			tmp[sizeof(tmp) - 1 - n++] = '-';
		}
		k1_put_field(&o, &tmp[sizeof(tmp) - n], n, width, left,
			     zero && !left ? '0' : ' ');
	}
	if (o.size)
		o.buf[o.len < o.size ? o.len : o.size - 1] = '\0';
	return (int)o.len;
}
