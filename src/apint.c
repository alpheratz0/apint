/*
	Copyright (C) 2022-2026 <alpheratz99@protonmail.com>

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

	 ________
	( tomcat )
	 --------
	   o
	    o

	     |\_/|
	     |o o|__
	     --*--__\
	     C_C_(___)

*/

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_cursor.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "color.h"
#include "log.h"
#include "utils.h"
#include "canvas.h"
#include "picker.h"
#include "history.h"
#include "toolbar.h"

typedef struct {
	bool active;
	int x;
	int y;
} DragInfo;

typedef struct {
	bool active;
	uint32_t color;
	int brush_size;
	xcb_point_t mouse_pos;
	int last_x;
	int last_y;
	bool has_prev;
	Tool tool;
	bool fill_mode;
} DrawInfo;

typedef struct {
	bool active;
	int start_vx, start_vy;
	int cur_vx, cur_vy;
} ShapeInfo;

#define APINT_WM_NAME "apint"
#define APINT_WM_CLASS "apint\0apint\0"
#define APINT_STROKE_SPACING_FACTOR 0.55f
#define APINT_MAX_BRUSH_SIZE (100)

#ifndef APINT_NO_HISTORY
#define APINT_HISTORY 1
#endif

#ifdef APINT_HISTORY
static History *hist;
static HistoryUserAction *hist_stroke;
#endif

static Canvas *canvas;
static Picker *picker;
static Toolbar *toolbar;
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t win;
static xcb_gcontext_t brush_preview_gc;
static xcb_gcontext_t shape_preview_gc;
static xcb_key_symbols_t *ksyms;
static xcb_cursor_context_t *cctx;
static xcb_cursor_t cursor_hand;
static xcb_cursor_t cursor_crosshair;
static DrawInfo drawinfo;
static DragInfo draginfo;
static ShapeInfo shapeinfo;
static bool start_in_fullscreen;
static bool should_close;

static xcb_atom_t
get_x11_atom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, &error);

	if (NULL != error)
		die("xcb_intern_atom failed with error code: %hhu",
				error->error_code);

	atom = reply->atom;
	free(reply);

	return atom;
}

static void
xwininit(void)
{
	xcb_atom_t _NET_WM_NAME,
			   _NET_WM_WINDOW_OPACITY;

	xcb_atom_t WM_PROTOCOLS,
			   WM_DELETE_WINDOW;

	xcb_atom_t _NET_WM_STATE,
			   _NET_WM_STATE_FULLSCREEN;

	xcb_atom_t UTF8_STRING;

	uint8_t opacity[4];

	conn = xcb_connect(NULL, NULL);

	if (xcb_connection_has_error(conn))
		die("can't open display");

	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	if (NULL == scr)
		die("can't get default screen");

	if (xcb_cursor_context_new(conn, scr, &cctx) != 0)
		die("can't create cursor context");

	cursor_hand = xcb_cursor_load_cursor(cctx, "fleur");
	cursor_crosshair = xcb_cursor_load_cursor(cctx, "crosshair");
	ksyms = xcb_key_symbols_alloc(conn);
	win = xcb_generate_id(conn);
	brush_preview_gc = xcb_generate_id(conn);
	shape_preview_gc = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, scr->root_depth, win, scr->root, 0, 0,
		800, 600, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		scr->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.background_pixel = 0x1e1e1e,
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);

	_NET_WM_NAME = get_x11_atom("_NET_WM_NAME");
	UTF8_STRING = get_x11_atom("UTF8_STRING");

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, _NET_WM_NAME,
			UTF8_STRING, 8, sizeof(APINT_WM_NAME) - 1, APINT_WM_NAME);

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, sizeof(APINT_WM_CLASS) - 1, APINT_WM_CLASS);

	WM_PROTOCOLS = get_x11_atom("WM_PROTOCOLS");
	WM_DELETE_WINDOW = get_x11_atom("WM_DELETE_WINDOW");

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
		WM_PROTOCOLS, XCB_ATOM_ATOM, 32, 1, &WM_DELETE_WINDOW);

	_NET_WM_WINDOW_OPACITY = get_x11_atom("_NET_WM_WINDOW_OPACITY");
	opacity[0] = opacity[1] = opacity[2] = opacity[3] = 0xff;

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
		_NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 32, 1, opacity);

	_NET_WM_STATE = get_x11_atom("_NET_WM_STATE");
	_NET_WM_STATE_FULLSCREEN = get_x11_atom("_NET_WM_STATE_FULLSCREEN");

	if (start_in_fullscreen) {
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
			_NET_WM_STATE, XCB_ATOM_ATOM, 32, 1, &_NET_WM_STATE_FULLSCREEN);
	}

	xcb_create_gc(conn, brush_preview_gc, win, XCB_GC_FOREGROUND, (const uint32_t[]){0xcccccc});
	xcb_create_gc(conn, shape_preview_gc, win,
		XCB_GC_FOREGROUND | XCB_GC_LINE_STYLE,
		(const uint32_t[]){0x000000, XCB_LINE_STYLE_ON_OFF_DASH});
	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_crosshair);
	xcb_map_window(conn, win);
	xcb_flush(conn);
}

static void
xwindestroy(void)
{
	xcb_free_gc(conn, brush_preview_gc);
	xcb_free_gc(conn, shape_preview_gc);
	xcb_free_cursor(conn, cursor_hand);
	xcb_free_cursor(conn, cursor_crosshair);
	xcb_key_symbols_free(ksyms);
	xcb_destroy_window(conn, win);
	xcb_cursor_context_free(cctx);
	xcb_disconnect(conn);
}

static void
brush_preview_render(void)
{
	canvas_render(canvas);

	xcb_poly_arc(conn, win, brush_preview_gc, 1, (const xcb_arc_t []) {{
		.x = drawinfo.mouse_pos.x - drawinfo.brush_size,
		.y = drawinfo.mouse_pos.y - drawinfo.brush_size,
		.width = drawinfo.brush_size*2,
		.height = drawinfo.brush_size*2,
		.angle1 = 0 << 6,
		.angle2 = 360 << 6
	}});

	xcb_flush(conn);
}

static void
addpoint(int x, int y, uint32_t color, int size)
{
	int dx, dy;
	uint32_t prevcol;

	for (dy = -size; dy < size; ++dy) {
		for (dx = -size; dx < size; ++dx) {
			if (dy * dy + dx * dx >= size * size ||
					!canvas_get_pixel(canvas, x + dx, y + dy, &prevcol))
				continue;
#ifdef APINT_USE_ROUGH_BRUSH
			canvas_set_pixel(canvas, x + dx, y + dy, color);
#else
			canvas_set_pixel(canvas, x + dx, y + dy,
					color_mix(color, prevcol,
						((sqrt(dy * dy + dx * dx)*0xff) / size)));
#endif
		}
	}
}

static void
addsegment(int x0, int y0, int x1, int y1, uint32_t color, int size)
{
	int dx = x1 - x0;
	int dy = y1 - y0;
	float dist = sqrtf((float)(dx*dx + dy*dy));
	if (dist <= 0.0f) {
		return;
	}

	float spacing = size * APINT_STROKE_SPACING_FACTOR;
	if (spacing < 1.0f) spacing = 1.0f;

	int steps = (int)ceilf(dist / spacing);
	float stepx = dx / (float)steps;
	float stepy = dy / (float)steps;

	for (int i = 1; i <= steps; ++i) {
		int xi = (int)lroundf(x0 + stepx * i);
		int yi = (int)lroundf(y0 + stepy * i);
		addpoint(xi, yi, color, size);
	}
}

static void
fill_hspan(int xa, int xb, int y, uint32_t color, int size)
{
	int x;

	if (xa > xb) {
		int t = xa; xa = xb; xb = t;
	}

	for (x = xa; x <= xb; x += size)
		addpoint(x, y, color, size);
	addpoint(xb, y, color, size);
}

static void
draw_rect(int x0, int y0, int x1, int y1, uint32_t color, int size, bool fill)
{
	int y;

	if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
	if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

	if (fill)
		for (y = y0; y <= y1; y += size)
			fill_hspan(x0, x1, y, color, size);

	addsegment(x0, y0, x1, y0, color, size);
	addsegment(x1, y0, x1, y1, color, size);
	addsegment(x1, y1, x0, y1, color, size);
	addsegment(x0, y1, x0, y0, color, size);
}

static void
draw_ellipse(int x0, int y0, int x1, int y1, uint32_t color, int size, bool fill)
{
	int cx, cy, rx, ry, i, steps;
	int px = 0, py = 0;
	float t, hw, k;

	cx = (x0 + x1) / 2;
	cy = (y0 + y1) / 2;
	rx = abs(x1 - x0) / 2;
	ry = abs(y1 - y0) / 2;

	if (rx <= 0 || ry <= 0)
		return;

	if (fill) {
		int dy;
		for (dy = -ry; dy <= ry; dy += size) {
			k = 1.0f - (float)(dy * dy) / (float)(ry * ry);
			if (k < 0.0f) k = 0.0f;
			hw = rx * sqrtf(k);
			fill_hspan(cx - (int)hw, cx + (int)hw, cy + dy, color, size);
		}
	}

	steps = (int)(2.0f * 3.14159265f * (rx > ry ? rx : ry));
	if (steps < 16) steps = 16;

	for (i = 0; i <= steps; ++i) {
		t = (2.0f * 3.14159265f * i) / steps;
		int nx = cx + (int)lroundf(rx * cosf(t));
		int ny = cy + (int)lroundf(ry * sinf(t));
		if (i > 0)
			addsegment(px, py, nx, ny, color, size);
		px = nx; py = ny;
	}
}

static void
draw_triangle(int x0, int y0, int x1, int y1, uint32_t color, int size, bool fill)
{
	int apexx, apexy, blx, bly, brx, bry, y, step;
	float t;

	apexx = (x0 + x1) / 2;
	apexy = y0;
	blx = x0; bly = y1;
	brx = x1; bry = y1;

	if (fill && bly != apexy) {
		/* interpolate from the apex towards the base, regardless of
		 * whether the triangle was drawn top-down or bottom-up */
		step = (bly > apexy) ? size : -size;
		for (y = apexy; (step > 0) ? (y <= bly) : (y >= bly); y += step) {
			t = (float)(y - apexy) / (float)(bly - apexy);
			int lx = apexx + (int)((blx - apexx) * t);
			int rx = apexx + (int)((brx - apexx) * t);
			fill_hspan(lx, rx, y, color, size);
		}
	}

	addsegment(apexx, apexy, blx, bly, color, size);
	addsegment(blx, bly, brx, bry, color, size);
	addsegment(brx, bry, apexx, apexy, color, size);
}

static void flood_fill(int sx, int sy, uint32_t newcolor);

/*
 * Paint a single user action onto the canvas. Used both when the action is
 * first performed and when rebuilding the canvas from history. It only draws;
 * it records nothing.
 */
static void
replay_action(const HistoryUserAction *a)
{
	int i;

	switch (a->type) {
	case HISTORY_STROKE:
		if (a->stroke.npoints > 0)
			addpoint(a->stroke.points[0].x, a->stroke.points[0].y,
					a->color, a->size);
		for (i = 1; i < a->stroke.npoints; ++i)
			addsegment(a->stroke.points[i-1].x, a->stroke.points[i-1].y,
					a->stroke.points[i].x, a->stroke.points[i].y,
					a->color, a->size);
		break;
	case HISTORY_LINE:
		addpoint(a->shape.x0, a->shape.y0, a->color, a->size);
		addsegment(a->shape.x0, a->shape.y0, a->shape.x1, a->shape.y1,
				a->color, a->size);
		break;
	case HISTORY_RECTANGLE:
		draw_rect(a->shape.x0, a->shape.y0, a->shape.x1, a->shape.y1,
				a->color, a->size, a->shape.fill);
		break;
	case HISTORY_ELLIPSE:
		draw_ellipse(a->shape.x0, a->shape.y0, a->shape.x1, a->shape.y1,
				a->color, a->size, a->shape.fill);
		break;
	case HISTORY_TRIANGLE:
		draw_triangle(a->shape.x0, a->shape.y0, a->shape.x1, a->shape.y1,
				a->color, a->size, a->shape.fill);
		break;
	case HISTORY_FILL:
		flood_fill(a->bucket.x, a->bucket.y, a->color);
		break;
	}
}

/*
 * Scanline flood fill. Only touches canvas pixels; it records nothing and
 * does not render, so it can be reused both for the initial fill and when
 * replaying a fill action from history.
 */
static void
flood_fill(int sx, int sy, uint32_t newcolor)
{
	uint32_t target, c;
	int *stack;
	size_t top, cap;
	int x, y, lx, rx, i, ny, dir;

	if (!canvas_get_pixel(canvas, sx, sy, &target))
		return;

	if (target == newcolor)
		return;

	cap = 256;
	top = 0;
	stack = xmalloc(cap * 2 * sizeof(int));
	stack[top*2] = sx; stack[top*2+1] = sy; ++top;

	while (top > 0) {
		--top;
		x = stack[top*2]; y = stack[top*2+1];

		if (!canvas_get_pixel(canvas, x, y, &c) || c != target)
			continue;

		/* grow the span to its left and right limits */
		lx = x;
		while (canvas_get_pixel(canvas, lx - 1, y, &c) && c == target)
			--lx;
		rx = x;
		while (canvas_get_pixel(canvas, rx + 1, y, &c) && c == target)
			++rx;

		for (i = lx; i <= rx; ++i)
			canvas_set_pixel(canvas, i, y, newcolor);

		/* seed contiguous runs of the target color on the rows above
		 * and below the span we just filled */
		for (dir = -1; dir <= 1; dir += 2) {
			ny = y + dir;
			i = lx;
			while (i <= rx) {
				if (!canvas_get_pixel(canvas, i, ny, &c) || c != target) {
					++i;
					continue;
				}
				while (i <= rx && canvas_get_pixel(canvas, i, ny, &c) &&
						c == target)
					++i;
				if (top >= cap) {
					cap *= 2;
					stack = xrealloc(stack, cap * 2 * sizeof(int));
				}
				stack[top*2] = i - 1; stack[top*2+1] = ny; ++top;
			}
		}
	}

	free(stack);
}

/*
 * Either store the action in the undo history or, when history is disabled,
 * free it (it has already been painted by the caller).
 */
static void
record_action(HistoryUserAction *a)
{
#ifdef APINT_HISTORY
	history_do(hist, a);
#else
	history_user_action_destroy(a);
#endif
}

static void
fillbucket(int sx, int sy, uint32_t newcolor)
{
	uint32_t target;
	HistoryUserAction *a;

	if (!canvas_get_pixel(canvas, sx, sy, &target) || target == newcolor)
		return;

	a = history_user_action_new();
	a->type = HISTORY_FILL;
	a->color = newcolor;
	a->bucket.x = sx;
	a->bucket.y = sy;

	replay_action(a);
	record_action(a);

	canvas_render(canvas);
}

static void
commit_shape(void)
{
	HistoryUserAction *a;
	HistoryActionType type;
	int x0, y0, x1, y1;

	switch (drawinfo.tool) {
	case TOOL_LINE:      type = HISTORY_LINE; break;
	case TOOL_RECTANGLE: type = HISTORY_RECTANGLE; break;
	case TOOL_ELLIPSE:   type = HISTORY_ELLIPSE; break;
	case TOOL_TRIANGLE:  type = HISTORY_TRIANGLE; break;
	default:             return;
	}

	canvas_viewport_to_canvas_pos(canvas, shapeinfo.start_vx,
			shapeinfo.start_vy, &x0, &y0);
	canvas_viewport_to_canvas_pos(canvas, shapeinfo.cur_vx,
			shapeinfo.cur_vy, &x1, &y1);

	a = history_user_action_new();
	a->type = type;
	a->color = drawinfo.color;
	a->size = drawinfo.brush_size;
	a->shape.x0 = x0; a->shape.y0 = y0;
	a->shape.x1 = x1; a->shape.y1 = y1;
	a->shape.fill = drawinfo.fill_mode;

	replay_action(a);
	record_action(a);

	canvas_render(canvas);
}

static void
shape_preview_render(void)
{
	int x0 = shapeinfo.start_vx, y0 = shapeinfo.start_vy;
	int x1 = shapeinfo.cur_vx, y1 = shapeinfo.cur_vy;

	canvas_render(canvas);

	switch (drawinfo.tool) {
	case TOOL_LINE:
		xcb_poly_line(conn, XCB_COORD_MODE_ORIGIN, win, shape_preview_gc, 2,
				(const xcb_point_t []){{ x0, y0 }, { x1, y1 }});
		break;
	case TOOL_RECTANGLE:
		xcb_poly_rectangle(conn, win, shape_preview_gc, 1,
				(const xcb_rectangle_t []){{
					MIN(x0, x1), MIN(y0, y1),
					abs(x1 - x0), abs(y1 - y0) }});
		break;
	case TOOL_ELLIPSE:
		xcb_poly_arc(conn, win, shape_preview_gc, 1, (const xcb_arc_t []){{
			.x = MIN(x0, x1), .y = MIN(y0, y1),
			.width = abs(x1 - x0), .height = abs(y1 - y0),
			.angle1 = 0, .angle2 = 360 << 6 }});
		break;
	case TOOL_TRIANGLE:
		xcb_poly_line(conn, XCB_COORD_MODE_ORIGIN, win, shape_preview_gc, 4,
				(const xcb_point_t []){
					{ (x0 + x1) / 2, y0 },
					{ x0, y1 }, { x1, y1 },
					{ (x0 + x1) / 2, y0 }});
		break;
	default:
		break;
	}

	xcb_flush(conn);
}

#ifdef APINT_HISTORY
static void
regenfromhist(void)
{
	HistoryUserAction *hua;

	for (hua = hist->root; hua != hist->current->next; hua = hua->next)
		replay_action(hua);
}

static void
undo(void)
{
	if (history_undo(hist)) {
		canvas_clear(canvas);
		regenfromhist();
		canvas_render(canvas);
	}
}

static void
redo(void)
{
	if (history_redo(hist)) {
		canvas_clear(canvas);
		regenfromhist();
		canvas_render(canvas);
	}
}
#endif

static void
save(void)
{
	char *path, *expanded_path;

	if (NULL == (path = xprompt("save as...")))
		return;

	if (NULL == (expanded_path = path_expand(path))) {
		info("could not expand path");
	} else if (path_is_writeable(expanded_path)) {
		canvas_save(canvas, expanded_path);
		info("saved drawing succesfully to %s", path);
	} else {
		info("can't save to %s", path);
	}

	free(path);
	free(expanded_path);
}

static void
h_client_message(xcb_client_message_event_t *ev)
{
	xcb_atom_t WM_DELETE_WINDOW;

	WM_DELETE_WINDOW = get_x11_atom("WM_DELETE_WINDOW");

	/* check if the wm sent a delete window message */
	/* https://www.x.org/docs/ICCCM/icccm.pdf */
	if (ev->data.data32[0] == WM_DELETE_WINDOW)
		should_close = true;
}

static void
h_expose(xcb_expose_event_t *ev)
{
	(void) ev;
	canvas_render(canvas);
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	xcb_keysym_t key;
	int draw_position_x, draw_position_y;
	uint32_t oldcolor;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if (picker_is_visible(picker))
		picker_hide(picker);

	if (ev->state & XCB_MOD_MASK_CONTROL) {
		switch (key) {
#ifdef APINT_HISTORY
		case XKB_KEY_z: if (!drawinfo.active) undo(); return;
		case XKB_KEY_y: if (!drawinfo.active) redo(); return;
#endif

		case XKB_KEY_s: save(); return;
		case XKB_KEY_g:
			canvas_viewport_to_canvas_pos(canvas, drawinfo.mouse_pos.x, drawinfo.mouse_pos.y, &draw_position_x, &draw_position_y);
			canvas_get_pixel(canvas, draw_position_x, draw_position_y, &drawinfo.color);
			toolbar_set_color(toolbar, drawinfo.color);
			return;
		}
	}

	oldcolor = drawinfo.color;

	switch (key) {
	case XKB_KEY_r: drawinfo.color = 0xffb81c00; break; /* Red */
	case XKB_KEY_g: drawinfo.color = 0xff50c878; break; /* Green */
	case XKB_KEY_b: drawinfo.color = 0xff1239e6; break; /* Blue */
	case XKB_KEY_w: drawinfo.color = 0xffffffff; break; /* White */
	case XKB_KEY_q: drawinfo.color = 0xff000000; break; /* Black */
	case XKB_KEY_o: drawinfo.color = 0xffcc551f; break; /* Orange */
	case XKB_KEY_y: drawinfo.color = 0xffffff00; break; /* Yellow */
	case XKB_KEY_f: drawinfo.color = 0xffca2c92; break; /* Fuchsia */
	case XKB_KEY_t: drawinfo.color = 0xff008080; break; /* Teal */
	case XKB_KEY_c: drawinfo.color = 0xfffffdd0; break; /* Cream */
	case XKB_KEY_z: drawinfo.color = 0x00000000; break; /* Transparent */
	}

	if (drawinfo.color != oldcolor)
		toolbar_set_color(toolbar, drawinfo.color);
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	int x, y;
	switch (ev->detail) {
	case XCB_BUTTON_INDEX_1:
		if (draginfo.active)
			break;
		picker_hide(picker);
		if (drawinfo.tool == TOOL_FREEHAND) {
			drawinfo.active = true;
			canvas_viewport_to_canvas_pos(canvas, ev->event_x, ev->event_y, &x, &y);
			drawinfo.last_x = x;
			drawinfo.last_y = y;
			drawinfo.has_prev = true;
			addpoint(x, y, drawinfo.color, drawinfo.brush_size);
#ifdef APINT_HISTORY
			hist_stroke = history_user_action_new();
			hist_stroke->type = HISTORY_STROKE;
			hist_stroke->color = drawinfo.color;
			hist_stroke->size = drawinfo.brush_size;
			history_user_action_push_point(hist_stroke, x, y);
#endif
			canvas_render(canvas);
		} else if (drawinfo.tool == TOOL_FILLBUCKET) {
			canvas_viewport_to_canvas_pos(canvas, ev->event_x, ev->event_y, &x, &y);
			fillbucket(x, y, drawinfo.color);
		} else {
			shapeinfo.active = true;
			shapeinfo.start_vx = shapeinfo.cur_vx = ev->event_x;
			shapeinfo.start_vy = shapeinfo.cur_vy = ev->event_y;
			shape_preview_render();
		}
		break;
	case XCB_BUTTON_INDEX_2:
		if (drawinfo.active)
			break;
		draginfo.active = true;
		draginfo.x = ev->event_x;
		draginfo.y = ev->event_y;
		xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_hand);
		xcb_flush(conn);
		break;
	case XCB_BUTTON_INDEX_3:
		picker_set(picker, drawinfo.color);;
		picker_show(picker, ev->event_x, ev->event_y);
		break;
	case XCB_BUTTON_INDEX_4:
		if (drawinfo.brush_size < APINT_MAX_BRUSH_SIZE)
			drawinfo.brush_size++;
		toolbar_set_brush_size(toolbar, drawinfo.brush_size);
		brush_preview_render();
		break;
	case XCB_BUTTON_INDEX_5:
		if (drawinfo.brush_size > 2)
			drawinfo.brush_size--;
		toolbar_set_brush_size(toolbar, drawinfo.brush_size);
		brush_preview_render();
		break;
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	int x, y;
	int dx, dy;

	drawinfo.mouse_pos.x = ev->event_x;
	drawinfo.mouse_pos.y = ev->event_y;

	if (draginfo.active) {
		dx = ev->event_x - draginfo.x;
		dy = ev->event_y - draginfo.y;

		draginfo.x = ev->event_x;
		draginfo.y = ev->event_y;

		canvas_move_relative(canvas, dx, dy);
		canvas_render(canvas);
	}

	if (drawinfo.active) {
		canvas_viewport_to_canvas_pos(canvas, ev->event_x, ev->event_y, &x, &y);
		addsegment(drawinfo.last_x, drawinfo.last_y, x, y,
				drawinfo.color, drawinfo.brush_size);
#ifdef APINT_HISTORY
		if (NULL != hist_stroke)
			history_user_action_push_point(hist_stroke, x, y);
#endif
		drawinfo.last_x = x;
		drawinfo.last_y = y;
		canvas_render(canvas);
	}

	if (shapeinfo.active) {
		shapeinfo.cur_vx = ev->event_x;
		shapeinfo.cur_vy = ev->event_y;
		shape_preview_render();
	}
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	switch (ev->detail) {
	case XCB_BUTTON_INDEX_1:
		if (shapeinfo.active) {
			shapeinfo.active = false;
			commit_shape();
			break;
		}
		drawinfo.active = false;
		drawinfo.has_prev = false;
#ifdef APINT_HISTORY
		if (NULL != hist_stroke) {
			history_do(hist, hist_stroke);
			hist_stroke = NULL;
		}
#endif
		break;
	case XCB_BUTTON_INDEX_2:
		draginfo.active = false;
		xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_crosshair);
		xcb_flush(conn);
		break;
	}
}

static void
h_configure_notify(xcb_configure_notify_event_t *ev)
{
	canvas_set_viewport(canvas, ev->width, ev->height);
	if (NULL != toolbar)
		toolbar_set_viewport_height(toolbar, ev->height);
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(ksyms, ev);
}

static void
h_picker_color_change(Picker *picker, uint32_t color)
{
	(void) picker;
	drawinfo.color = color;
	toolbar_set_color(toolbar, color);
}

static void
h_toolbar_color_change(void *ud, uint32_t color)
{
	(void) ud;
	drawinfo.color = color;
}

static void
h_toolbar_brush_size_change(void *ud, int size)
{
	(void) ud;
	drawinfo.brush_size = size;
}

static void
h_toolbar_tool_change(void *ud, Tool tool)
{
	(void) ud;
	drawinfo.tool = tool;
}

static void
h_toolbar_fill_mode_change(void *ud, bool fill)
{
	(void) ud;
	drawinfo.fill_mode = fill;
}

static void
h_toolbar_save(void *ud)
{
	(void) ud;
	save();
}

static void
h_toolbar_undo(void *ud)
{
	(void) ud;
#ifdef APINT_HISTORY
	if (!drawinfo.active)
		undo();
#endif
}

static void
h_toolbar_redo(void *ud)
{
	(void) ud;
#ifdef APINT_HISTORY
	if (!drawinfo.active)
		redo();
#endif
}

static void
usage(void)
{
	puts("usage: apint [-fhv] [-l file] [-s size] [-b bg_color]");
	exit(0);
}

static void
version(void)
{
	puts("apint version "VERSION);
	exit(0);
}

int
main(int argc, char **argv)
{
	const char *loadpath;
	xcb_generic_event_t *ev;
	uint32_t bg;
	int width, height;

	bg = 0xffffffff;
	width = 640, height = 480;
	loadpath = NULL;

	while (++argv, --argc > 0) {
		if ((*argv)[0] == '-' && (*argv)[1] != '\0' && (*argv)[2] == '\0') {
			switch ((*argv)[1]) {
			case 'h': usage(); break;
			case 'v': version(); break;
			case 'f': start_in_fullscreen = true; break;
			case 'l': --argc; loadpath = enotnull(*++argv, "path"); break;
			case 's': --argc; size_parse(enotnull(*++argv, "size"), &width, &height); break;
			case 'b': --argc; color_parse(enotnull(*++argv, "bg_color"), &bg); break;
			default: die("invalid option %s", *argv); break;
			}
		} else {
			die("unexpected argument: %s", *argv);
		}
	}

	if (width <= 5 || height <= 5)
		die("invalid size");

	if (width > 5000)
		die("image too wide (max-width: 5000px)");

	if (height > 5000)
		die("image too tall (max-height: 5000px)");

	xwininit();

	drawinfo.color = 0xff000000;
	drawinfo.brush_size = 5;
	drawinfo.has_prev = false;
	drawinfo.tool = TOOL_FREEHAND;
	drawinfo.fill_mode = false;

	if (NULL == loadpath) {
		canvas = canvas_new(conn, win, width, height, bg);
	} else {
		canvas = canvas_load(conn, win, loadpath);
	}

	picker = picker_new(conn, win, h_picker_color_change);

	toolbar = toolbar_new(conn, win, (ToolbarCallbacks){
		.ud = NULL,
		.on_color = h_toolbar_color_change,
		.on_brush_size = h_toolbar_brush_size_change,
		.on_tool = h_toolbar_tool_change,
		.on_fill_mode = h_toolbar_fill_mode_change,
		.on_save = h_toolbar_save,
		.on_undo = h_toolbar_undo,
		.on_redo = h_toolbar_redo,
	});

#ifdef APINT_HISTORY
	hist = history_new();
#endif

	while (!should_close && (ev = xcb_wait_for_event(conn))) {

		// check if it is an event targeted to our color picker
		if (picker_try_process_event(picker, ev)) {
			free(ev);
			continue;
		}

		// check if it is an event targeted to the toolbar
		if (toolbar_try_process_event(toolbar, ev)) {
			free(ev);
			continue;
		}

		switch (ev->response_type & ~0x80) {
		case XCB_CLIENT_MESSAGE:     h_client_message((void *)(ev)); break;
		case XCB_EXPOSE:             h_expose((void *)(ev)); break;
		case XCB_KEY_PRESS:          h_key_press((void *)(ev)); break;
		case XCB_BUTTON_PRESS:       h_button_press((void *)(ev)); break;
		case XCB_MOTION_NOTIFY:      h_motion_notify((void *)(ev)); break;
		case XCB_BUTTON_RELEASE:     h_button_release((void *)(ev)); break;
		case XCB_CONFIGURE_NOTIFY:   h_configure_notify((void *)(ev)); break;
		case XCB_MAPPING_NOTIFY:     h_mapping_notify((void *)(ev)); break;
		}

		free(ev);
	}

#ifdef APINT_HISTORY
	history_destroy(hist);
#endif

	canvas_free(canvas);
	picker_free(picker);
	toolbar_free(toolbar);
	xwindestroy();

	return 0;
}
