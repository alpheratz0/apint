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
#include <xcb/xcb.h>
#include <xcb/xproto.h>

typedef struct Canvas Canvas;

extern Canvas *
canvas_new(xcb_connection_t *conn, xcb_window_t win, int w, int h, uint32_t bg);

extern Canvas *
canvas_load(xcb_connection_t *conn, xcb_window_t win, const char *path);

extern void
canvas_save(const Canvas *c, const char *path);

extern void
canvas_move_relative(Canvas *c, int offx, int offy);

extern void
canvas_set_viewport(Canvas *c, int vw, int vh);

extern void
canvas_render(Canvas *c);

extern void
canvas_set_pixel(Canvas *c, int x, int y, uint32_t color);

extern int
canvas_get_pixel(Canvas *c, int x, int y, uint32_t *color);

extern void
canvas_viewport_to_canvas_pos(Canvas *c, int x, int y, int *out_x, int *out_y);

extern void
canvas_clear(Canvas *c);

extern void
canvas_destroy(Canvas *c);
