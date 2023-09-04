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

#pragma once

#include <stdint.h>

#define RED(c) ((c>>16) & 0xff)
#define GREEN(c) ((c>>8) & 0xff)
#define BLUE(c) ((c>>0) & 0xff)
#define ALPHA(c) ((c>>24) & 0xff)

extern uint32_t
color_pack_from_arr(uint8_t *p);

extern void
color_unpack_to_arr(uint32_t c, uint8_t *p);

extern void
color_parse(const char *str, uint32_t *c);

extern uint32_t
color_mix(uint32_t c1, uint32_t c2, uint8_t alpha);
