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

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

#include "log.h"
#include "utils.h"
#include "toolbar.h"

/* layout */
#define LABELW   (14)
#define PAD      (8)
#define BTN      (26)
#define GAP      (4)
#define PITCH    (BTN + GAP)
#define COLX(i)  (LABELW + PAD + (i) * PITCH)
#define TB_WIDTH (COLX(2) + BTN + PAD + 15)

/* colors */
#define C_BG          (0xffffff)
#define C_BORDER      (0xc8c8c8)
#define C_ICON        (0x333333)
#define C_SEL_BG      (0xdbeafe)
#define C_SEL_BORDER  (0x2f7ef0)
#define C_LABEL       (0x909090)
#define C_SWBORDER    (0x9a9a9a)

typedef enum {
	R_SAVE,
	R_UNDO,
	R_REDO,
	R_FILL,
	R_STROKE,
	R_COLOR,
	R_ERASER,
	R_THICK,
	R_TOOL
} RKind;

typedef struct {
	int x, y, w, h;
	RKind kind;
	int data;
} Region;

#define MAX_REGIONS (64)

static const uint32_t palette[] = {
	0xff000000, 0xff808080, 0xffffffff,
	0xffb01030, 0xffe23030, 0xfff77f5a,
	0xfff5a623, 0xfff8c70a, 0xfff5e000,
	0xff1f8f3a, 0xff3cc24b, 0xff66e060,
	0xff1453c4, 0xff2f7ef0, 0xff35b6f5,
	0xff5e1b8f, 0xff8a3fd0, 0xffb57ff0,
	0xffe83b8f, 0xfff09cc0
};

#define PALETTE_LEN ((int)(sizeof(palette) / sizeof(palette[0])))

static const int thickness_presets[] = { 2, 5, 10, 18, 30 };

#define THICKNESS_LEN ((int)(sizeof(thickness_presets) / sizeof(thickness_presets[0])))

struct Toolbar {
	xcb_connection_t *conn;
	xcb_window_t win;
	xcb_gcontext_t gc;
	xcb_gcontext_t fontgc;
	xcb_font_t font;
	bool has_font;
	int height;
	ToolbarCallbacks cb;

	Region regions[MAX_REGIONS];
	int nregions;

	Tool sel_tool;
	uint32_t sel_color;
	int sel_size;
	bool fill_mode;

	/* vertical offsets of the labelled sections, computed on build */
	int colors_y0, thick_y0, shapes_y0;
};

static uint8_t
__x_get_screen_depth(xcb_connection_t *conn)
{
	xcb_screen_t *screen;
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	if (NULL == screen)
		die("can't get default screen");
	return screen->root_depth;
}

static xcb_visualid_t
__x_get_screen_visual(xcb_connection_t *conn)
{
	xcb_screen_t *screen;
	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	if (NULL == screen)
		die("can't get default screen");
	return screen->root_visual;
}

/* drawing helpers (operate directly on the window) */
static void
__toolbar_set_fg(Toolbar *tb, uint32_t color, uint32_t lw)
{
	xcb_change_gc(tb->conn, tb->gc,
			XCB_GC_FOREGROUND | XCB_GC_LINE_WIDTH,
			(const uint32_t []){ color, lw });
}

static void
__toolbar_fillrect(Toolbar *tb, int x, int y, int w, int h, uint32_t color)
{
	__toolbar_set_fg(tb, color, 1);
	xcb_poly_fill_rectangle(tb->conn, tb->win, tb->gc, 1,
			(const xcb_rectangle_t []){{ x, y, w, h }});
}

static void
__toolbar_strokerect(Toolbar *tb, int x, int y, int w, int h, uint32_t color, uint32_t lw)
{
	__toolbar_set_fg(tb, color, lw);
	xcb_poly_rectangle(tb->conn, tb->win, tb->gc, 1,
			(const xcb_rectangle_t []){{ x, y, w, h }});
}

static void
__toolbar_line(Toolbar *tb, int x0, int y0, int x1, int y1, uint32_t color, uint32_t lw)
{
	__toolbar_set_fg(tb, color, lw);
	xcb_poly_line(tb->conn, XCB_COORD_MODE_ORIGIN, tb->win, tb->gc, 2,
			(const xcb_point_t []){{ x0, y0 }, { x1, y1 }});
}

static void
__toolbar_polyline(Toolbar *tb, const xcb_point_t *pts, int n, uint32_t color, uint32_t lw)
{
	__toolbar_set_fg(tb, color, lw);
	xcb_poly_line(tb->conn, XCB_COORD_MODE_ORIGIN, tb->win, tb->gc, n, pts);
}

static void
__toolbar_circle(Toolbar *tb, int cx, int cy, int r, uint32_t color, uint32_t lw)
{
	__toolbar_set_fg(tb, color, lw);
	xcb_poly_arc(tb->conn, tb->win, tb->gc, 1, (const xcb_arc_t []){{
		.x = cx - r, .y = cy - r, .width = r * 2, .height = r * 2,
		.angle1 = 0, .angle2 = 360 << 6
	}});
}

static void
__toolbar_disc(Toolbar *tb, int cx, int cy, int r, uint32_t color)
{
	__toolbar_set_fg(tb, color, 1);
	xcb_poly_fill_arc(tb->conn, tb->win, tb->gc, 1, (const xcb_arc_t []){{
		.x = cx - r, .y = cy - r, .width = r * 2, .height = r * 2,
		.angle1 = 0, .angle2 = 360 << 6
	}});
}

static void
__toolbar_text(Toolbar *tb, int x, int y, const char *s)
{
	if (!tb->has_font)
		return;
	xcb_image_text_8(tb->conn, strlen(s), tb->win, tb->fontgc, x, y, s);
}

static void
__toolbar_vtext(Toolbar *tb, int cx, int cy, const char *s)
{
	int i, n, sy;
	if (!tb->has_font)
		return;
	n = strlen(s);
	sy = cy - (n * 9) / 2 + 8;
	for (i = 0; i < n; ++i)
		xcb_image_text_8(tb->conn, 1, tb->win, tb->fontgc,
				cx - 3, sy + i * 9, &s[i]);
}

/* region table */
static void
__toolbar_add_region(Toolbar *tb, int x, int y, int w, int h, RKind kind, int data)
{
	if (tb->nregions >= MAX_REGIONS)
		return;
	tb->regions[tb->nregions++] = (Region){ x, y, w, h, kind, data };
}

static void
__toolbar_build_regions(Toolbar *tb)
{
	int row, col, i;

	tb->nregions = 0;

	/* action row */
	__toolbar_add_region(tb, COLX(0), 8, BTN, BTN, R_SAVE, 0);
	__toolbar_add_region(tb, COLX(1), 8, BTN, BTN, R_UNDO, 0);
	__toolbar_add_region(tb, COLX(2), 8, BTN, BTN, R_REDO, 0);

	/* fill / stroke */
	__toolbar_add_region(tb, COLX(0), 44, 40, 16, R_FILL, 0);
	__toolbar_add_region(tb, COLX(1) + 18, 44, 50, 16, R_STROKE, 0);

	/* colors */
	tb->colors_y0 = 68;
	for (i = 0; i < PALETTE_LEN; ++i) {
		row = i / 3;
		col = i % 3;
		__toolbar_add_region(tb, COLX(col), tb->colors_y0 + row * PITCH,
				BTN, BTN, R_COLOR, i);
	}
	/* eraser (transparent color) follows the palette */
	__toolbar_add_region(tb, COLX(PALETTE_LEN % 3),
			tb->colors_y0 + (PALETTE_LEN / 3) * PITCH,
			BTN, BTN, R_ERASER, 0);

	/* thickness (color grid spans ceil((PALETTE_LEN + 1) / 3) rows) */
	tb->thick_y0 = tb->colors_y0 + ((PALETTE_LEN + 1 + 2) / 3) * PITCH + 8;
	{
		int total = COLX(2) + BTN - COLX(0);
		int cw = total / THICKNESS_LEN;
		for (i = 0; i < THICKNESS_LEN; ++i)
			__toolbar_add_region(tb, COLX(0) + i * cw, tb->thick_y0,
					cw, 46, R_THICK, i);
	}

	/* shapes */
	tb->shapes_y0 = tb->thick_y0 + 46 + 12;
	for (i = 0; i < 6; ++i) {
		row = i / 3;
		col = i % 3;
		__toolbar_add_region(tb, COLX(col), tb->shapes_y0 + row * PITCH,
				BTN, BTN, R_TOOL, i);
	}
}

/* icon rendering inside a button rect */
static void
__toolbar_btn_bg(Toolbar *tb, const Region *r, bool selected)
{
	if (selected)
		__toolbar_fillrect(tb, r->x, r->y, r->w, r->h, C_SEL_BG);
	__toolbar_strokerect(tb, r->x, r->y, r->w - 1, r->h - 1,
			selected ? C_SEL_BORDER : C_BORDER,
			selected ? 2 : 1);
}

static void
__toolbar_draw_icon(Toolbar *tb, const Region *r)
{
	int ix = r->x + 5, iy = r->y + 5, s = BTN - 10;
	int cx = r->x + BTN / 2, cy = r->y + BTN / 2;
	uint32_t col = C_ICON;

	switch (r->kind) {
	case R_SAVE:
		__toolbar_strokerect(tb, ix, iy, s, s, col, 1);
		__toolbar_fillrect(tb, ix + 4, iy + s - 7, s - 8, 6, col);
		__toolbar_fillrect(tb, ix + 4, iy + 2, s - 10, 4, col);
		break;
	case R_UNDO:
		__toolbar_set_fg(tb, col, 1);
		xcb_poly_arc(tb->conn, tb->win, tb->gc, 1, (const xcb_arc_t []){{
			.x = cx - 6, .y = cy - 6, .width = 12, .height = 12,
			.angle1 = 240 << 6, .angle2 = 270 << 6
		}});
		__toolbar_line(tb, cx - 5, cy - 3, cx - 9, cy - 4, col, 1);
		__toolbar_line(tb, cx - 5, cy - 3, cx - 6, cy + 1, col, 1);
		break;
	case R_REDO:
		__toolbar_set_fg(tb, col, 1);
		xcb_poly_arc(tb->conn, tb->win, tb->gc, 1, (const xcb_arc_t []){{
			.x = cx - 6, .y = cy - 6, .width = 12, .height = 12,
			.angle1 = 30 << 6, .angle2 = 270 << 6
		}});
		__toolbar_line(tb, cx + 5, cy - 3, cx + 9, cy - 4, col, 1);
		__toolbar_line(tb, cx + 5, cy - 3, cx + 6, cy + 1, col, 1);
		break;
	case R_TOOL:
		switch (r->data) {
		case TOOL_FREEHAND:
			__toolbar_disc(tb, cx, cy, 4, col);
			break;
		case TOOL_LINE:
			__toolbar_line(tb, ix, iy + s, ix + s, iy, col, 1);
			break;
		case TOOL_RECTANGLE:
			__toolbar_strokerect(tb, ix, iy + 2, s, s - 4, col, 1);
			break;
		case TOOL_ELLIPSE:
			__toolbar_circle(tb, cx, cy, 8, col, 1);
			break;
		case TOOL_TRIANGLE:
			__toolbar_polyline(tb, (const xcb_point_t []){
				{ cx, iy }, { ix, iy + s },
				{ ix + s, iy + s }, { cx, iy }
			}, 4, col, 1);
			break;
		case TOOL_FILLBUCKET:
			__toolbar_polyline(tb, (const xcb_point_t []){
				{ ix + 1, iy + 4 }, { ix + 12, iy + 4 },
				{ ix + 9, iy + 15 }, { ix + 4, iy + 15 },
				{ ix + 1, iy + 4 }
			}, 5, col, 1);
			__toolbar_line(tb, ix + 3, iy + 4, ix + 6, iy + 1, col, 1);
			__toolbar_line(tb, ix + 6, iy + 1, ix + 10, iy + 3, col, 1);
			__toolbar_disc(tb, ix + 13, iy + 13, 2, col);
			break;
		}
		break;
	default:
		break;
	}
}

static void
__toolbar_draw(Toolbar *tb)
{
	int i;

	__toolbar_fillrect(tb, 0, 0, TB_WIDTH, tb->height, C_BG);

	for (i = 0; i < tb->nregions; ++i) {
		Region *r = &tb->regions[i];
		bool selected = false;

		switch (r->kind) {
		case R_COLOR:
			__toolbar_fillrect(tb, r->x, r->y, BTN, BTN, palette[r->data]);
			selected = (tb->sel_color == palette[r->data]);
			__toolbar_strokerect(tb, r->x, r->y, BTN - 1, BTN - 1,
					selected ? C_SEL_BORDER : C_SWBORDER,
					selected ? 2 : 1);
			break;
		case R_FILL:
			selected = tb->fill_mode;
			__toolbar_circle(tb, r->x + 5, r->y + 8, 4, C_ICON, 1);
			if (selected)
				__toolbar_disc(tb, r->x + 5, r->y + 8, 2, C_ICON);
			__toolbar_text(tb, r->x + 14, r->y + 12, "fill");
			break;
		case R_STROKE:
			selected = !tb->fill_mode;
			__toolbar_circle(tb, r->x + 5, r->y + 8, 4, C_ICON, 1);
			if (selected)
				__toolbar_disc(tb, r->x + 5, r->y + 8, 2, C_ICON);
			__toolbar_text(tb, r->x + 14, r->y + 12, "stroke");
			break;
		case R_THICK: {
			int bw = 1 + r->data * 2;
			selected = (tb->sel_size == thickness_presets[r->data]);
			if (selected)
				__toolbar_fillrect(tb, r->x, r->y, r->w, r->h, C_SEL_BG);
			__toolbar_strokerect(tb, r->x, r->y, r->w - 1, r->h - 1, C_BORDER, 1);
			__toolbar_fillrect(tb, r->x + r->w / 2 - bw / 2, r->y + 7,
					bw, r->h - 14, C_ICON);
			break;
		}
		case R_TOOL:
			selected = ((int)tb->sel_tool == r->data);
			__toolbar_btn_bg(tb, r, selected);
			__toolbar_draw_icon(tb, r);
			break;
		case R_ERASER: {
			int gx, gy, gw, gh;
			selected = (tb->sel_color == 0x00000000);
			for (gy = 0; gy < BTN; gy += 6) {
				for (gx = 0; gx < BTN; gx += 6) {
					gw = (gx + 6 > BTN) ? BTN - gx : 6;
					gh = (gy + 6 > BTN) ? BTN - gy : 6;
					__toolbar_fillrect(tb, r->x + gx, r->y + gy, gw, gh,
							((gx / 6 + gy / 6) % 2) ? 0xffffff : 0xcfcfcf);
				}
			}
			__toolbar_strokerect(tb, r->x, r->y, BTN - 1, BTN - 1,
					selected ? C_SEL_BORDER : C_SWBORDER,
					selected ? 2 : 1);
			break;
		}
		default:
			__toolbar_btn_bg(tb, r, false);
			__toolbar_draw_icon(tb, r);
			break;
		}
	}

	/* section labels */
	__toolbar_vtext(tb, 6, tb->colors_y0 + ((PALETTE_LEN + 1 + 2) / 3) * PITCH / 2, "colors");
	__toolbar_vtext(tb, 6, tb->thick_y0 + 23, "thickness");
	__toolbar_vtext(tb, 6, tb->shapes_y0 + PITCH, "shapes");

	xcb_flush(tb->conn);
}

extern Toolbar *
toolbar_new(xcb_connection_t *conn, xcb_window_t parent_win, ToolbarCallbacks cb)
{
	Toolbar *tb;
	xcb_void_cookie_t fc;

	tb = xcalloc(1, sizeof(Toolbar));

	tb->conn = conn;
	tb->cb = cb;
	tb->height = 600;
	tb->win = xcb_generate_id(conn);
	tb->gc = xcb_generate_id(conn);
	tb->fontgc = xcb_generate_id(conn);
	tb->font = xcb_generate_id(conn);

	tb->sel_tool = TOOL_FREEHAND;
	tb->sel_color = 0xff000000;
	tb->sel_size = 5;
	tb->fill_mode = false;

	xcb_create_window_aux(
		conn, __x_get_screen_depth(conn),
		tb->win, parent_win, 0, 0,
		TB_WIDTH, tb->height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		__x_get_screen_visual(conn), XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_BUTTON_PRESS
		}}
	);

	xcb_create_gc(conn, tb->gc, tb->win, 0, NULL);

	fc = xcb_open_font_checked(conn, tb->font, 5, "fixed");
	if (NULL == xcb_request_check(conn, fc)) {
		tb->has_font = true;
		xcb_create_gc(conn, tb->fontgc, tb->win,
				XCB_GC_FOREGROUND | XCB_GC_BACKGROUND | XCB_GC_FONT,
				(const uint32_t []){ C_LABEL, C_BG, tb->font });
	}

	__toolbar_build_regions(tb);

	xcb_map_window(conn, tb->win);
	xcb_flush(conn);

	return tb;
}

extern void
toolbar_set_viewport_height(Toolbar *tb, int height)
{
	tb->height = height;
	xcb_configure_window(tb->conn, tb->win,
			XCB_CONFIG_WINDOW_HEIGHT,
			(const uint32_t []){ (uint32_t)height });
	__toolbar_draw(tb);
}

static void
__toolbar_dispatch(Toolbar *tb, Region *r)
{
	switch (r->kind) {
	case R_SAVE:
		if (tb->cb.on_save) tb->cb.on_save(tb->cb.ud);
		break;
	case R_UNDO:
		if (tb->cb.on_undo) tb->cb.on_undo(tb->cb.ud);
		break;
	case R_REDO:
		if (tb->cb.on_redo) tb->cb.on_redo(tb->cb.ud);
		break;
	case R_FILL:
		tb->fill_mode = true;
		if (tb->cb.on_fill_mode) tb->cb.on_fill_mode(tb->cb.ud, true);
		__toolbar_draw(tb);
		break;
	case R_STROKE:
		tb->fill_mode = false;
		if (tb->cb.on_fill_mode) tb->cb.on_fill_mode(tb->cb.ud, false);
		__toolbar_draw(tb);
		break;
	case R_COLOR:
		tb->sel_color = palette[r->data];
		if (tb->cb.on_color) tb->cb.on_color(tb->cb.ud, tb->sel_color);
		__toolbar_draw(tb);
		break;
	case R_ERASER:
		tb->sel_color = 0x00000000;
		if (tb->cb.on_color) tb->cb.on_color(tb->cb.ud, 0x00000000);
		__toolbar_draw(tb);
		break;
	case R_THICK:
		tb->sel_size = thickness_presets[r->data];
		if (tb->cb.on_brush_size)
			tb->cb.on_brush_size(tb->cb.ud, tb->sel_size);
		__toolbar_draw(tb);
		break;
	case R_TOOL:
		tb->sel_tool = (Tool)r->data;
		if (tb->cb.on_tool) tb->cb.on_tool(tb->cb.ud, tb->sel_tool);
		__toolbar_draw(tb);
		break;
	}
}

static bool
__h_toolbar_expose(Toolbar *tb, const xcb_expose_event_t *ev)
{
	if (ev->window != tb->win)
		return false;
	__toolbar_draw(tb);
	return true;
}

static bool
__h_toolbar_button_press(Toolbar *tb, const xcb_button_press_event_t *ev)
{
	int i;

	if (ev->event != tb->win)
		return false;

	if (ev->detail == XCB_BUTTON_INDEX_1) {
		for (i = 0; i < tb->nregions; ++i) {
			Region *r = &tb->regions[i];
			if (ev->event_x >= r->x && ev->event_x < r->x + r->w &&
					ev->event_y >= r->y && ev->event_y < r->y + r->h) {
				__toolbar_dispatch(tb, r);
				break;
			}
		}
	}

	return true;
}

extern bool
toolbar_try_process_event(Toolbar *tb, const xcb_generic_event_t *ge)
{
	switch (ge->response_type & ~0x80) {
	case XCB_EXPOSE:        return __h_toolbar_expose(tb, (void *)(ge));
	case XCB_BUTTON_PRESS:  return __h_toolbar_button_press(tb, (void *)(ge));
	default:                return false;
	}
}

extern void
toolbar_set_color(Toolbar *tb, uint32_t color)
{
	tb->sel_color = color;
	__toolbar_draw(tb);
}

extern void
toolbar_set_brush_size(Toolbar *tb, int size)
{
	tb->sel_size = size;
	__toolbar_draw(tb);
}

extern void
toolbar_set_tool(Toolbar *tb, Tool tool)
{
	tb->sel_tool = tool;
	__toolbar_draw(tb);
}

extern void
toolbar_set_fill_mode(Toolbar *tb, bool fill)
{
	tb->fill_mode = fill;
	__toolbar_draw(tb);
}

extern void
toolbar_free(Toolbar *tb)
{
	if (tb->has_font) {
		xcb_close_font(tb->conn, tb->font);
		xcb_free_gc(tb->conn, tb->fontgc);
	}
	xcb_free_gc(tb->conn, tb->gc);
	xcb_destroy_window(tb->conn, tb->win);
	free(tb);
}
