/*
	Copyright (C) 2026 <alpheratz99@protonmail.com>

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
#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

typedef struct Toolbar Toolbar;

typedef enum {
	TOOL_FREEHAND,
	TOOL_LINE,
	TOOL_RECTANGLE,
	TOOL_ELLIPSE,
	TOOL_TRIANGLE,
	TOOL_FILLBUCKET
} Tool;

typedef struct {
	void *ud;
	void (*on_color)(void *ud, uint32_t color);
	void (*on_brush_size)(void *ud, int size);
	void (*on_tool)(void *ud, Tool tool);
	void (*on_fill_mode)(void *ud, bool fill);
	void (*on_save)(void *ud);
	void (*on_undo)(void *ud);
	void (*on_redo)(void *ud);
} ToolbarCallbacks;

extern Toolbar *
toolbar_new(xcb_connection_t *conn, xcb_window_t parent_win,
		ToolbarCallbacks cb);

extern void
toolbar_set_viewport_height(Toolbar *tb, int height);

extern bool
toolbar_try_process_event(Toolbar *tb, const xcb_generic_event_t *ge);

extern void
toolbar_set_color(Toolbar *tb, uint32_t color);

extern void
toolbar_set_brush_size(Toolbar *tb, int size);

extern void
toolbar_set_tool(Toolbar *tb, Tool tool);

extern void
toolbar_set_fill_mode(Toolbar *tb, bool fill);

extern void
toolbar_free(Toolbar *tb);
