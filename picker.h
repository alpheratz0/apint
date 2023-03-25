/*
	Copyright (C) 2023 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License version 2 as published by the
	Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along with
	this program; if not, write to the Free Software Foundation, Inc., 59 Temple
	Place, Suite 330, Boston, MA 02111-1307 USA

*/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

typedef struct Picker Picker;

typedef void (*PickerOnColorChangeHandler)(Picker *picker, uint32_t color);

extern Picker *
picker_new(xcb_connection_t *conn, xcb_window_t parent_win, PickerOnColorChangeHandler occ);

extern bool
picker_try_process_event(Picker *picker, const xcb_generic_event_t *ge);

extern bool
picker_is_visible(const Picker *picker);

extern void
picker_show(Picker *picker, int x, int y);

extern void
picker_hide(Picker *picker);

extern void
picker_set(Picker *picker, uint32_t color);

extern void
picker_destroy(Picker *picker);
