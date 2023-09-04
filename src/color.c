/*
	Copyright (C) 2023 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License version 2 as published by
	the Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
	more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc., 59
	Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#include <stdlib.h>
#include <stdint.h>
#include "color.h"

static inline uint8_t
__alpha_blend(uint8_t a, uint8_t b, uint8_t alpha)
{
	return (a + ((b - a) * alpha) / 255);
}

static inline uint32_t
__color_pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return (uint32_t)(
		(a<<24)|
		(r<<16)|
		(g<<8)|
		(b<<0)
	);
}

extern uint32_t
color_pack_from_arr(uint8_t *p)
{
	return (uint32_t)(
		(p[3]<<24)|
		(p[0]<<16)|
		(p[1]<<8)|
		(p[2]<<0)
	);
}

extern void
color_unpack_to_arr(uint32_t c, uint8_t *p)
{
	p[0] = RED(c);
	p[1] = GREEN(c);
	p[2] = BLUE(c);
	p[3] = ALPHA(c);
}

extern void
color_parse(const char *str, uint32_t *c)
{
	uint32_t tmp_col;
	char *end = NULL;

	if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
		str += 2;
	} else if (str[0] == '#') {
		str += 1;
	}

	tmp_col = strtol(str, &end, 16);

	if (*end != '\0')
		return;

	*c = tmp_col;

	if (end - str <= 6)
		*c |= 0xff << 24;
}

extern uint32_t
color_mix(uint32_t c1, uint32_t c2, uint8_t alpha)
{
	uint32_t mixed;
	uint8_t c1_u[4], c2_u[4];

	color_unpack_to_arr(c1, c1_u);
	color_unpack_to_arr(c2, c2_u);

	mixed = __color_pack(
		__alpha_blend(c1_u[0], c2_u[0], alpha),
		__alpha_blend(c1_u[1], c2_u[1], alpha),
		__alpha_blend(c1_u[2], c2_u[2], alpha),
		__alpha_blend(c1_u[3], c2_u[3], alpha)
	);

	return mixed;
}
