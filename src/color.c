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

#include <stdint.h>
#include "color.h"

extern uint32_t
color_mix(uint32_t c1, uint32_t c2, uint8_t alpha)
{
	uint8_t r, g, b, a;
	uint8_t c1_r, c1_g, c1_b, c1_a;
	uint8_t c2_r, c2_g, c2_b, c2_a;
	uint32_t mixed;

	c1_r = RED(c1);   c2_r = RED(c2);
	c1_g = GREEN(c1); c2_g = GREEN(c2);
	c1_b = BLUE(c1);  c2_b = BLUE(c2);
	c1_a = ALPHA(c1); c2_a = ALPHA(c2);

	r = ALPHA_BLEND(c1_r, c2_r, alpha);
	g = ALPHA_BLEND(c1_g, c2_g, alpha);
	b = ALPHA_BLEND(c1_b, c2_b, alpha);
	a = ALPHA_BLEND(c1_a, c2_a, alpha);

	mixed = COLOR(r, g, b, a);

	return mixed;
}
