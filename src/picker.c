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
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>

#include "picker.h"
#include "utils.h"

#define PADDING 10

#define SATURATION_LIGHTNESS_RECT_X1 (PADDING)
#define SATURATION_LIGHTNESS_RECT_Y1 (PADDING)
#define SATURATION_LIGHTNESS_RECT_WIDTH (200)
#define SATURATION_LIGHTNESS_RECT_HEIGHT (SATURATION_LIGHTNESS_RECT_WIDTH)
#define SATURATION_LIGHTNESS_RECT_X2 (SATURATION_LIGHTNESS_RECT_X1 + SATURATION_LIGHTNESS_RECT_WIDTH)
#define SATURATION_LIGHTNESS_RECT_Y2 (SATURATION_LIGHTNESS_RECT_Y1 + SATURATION_LIGHTNESS_RECT_HEIGHT)

#define HUE_RECT_X1 (SATURATION_LIGHTNESS_RECT_X2 + PADDING)
#define HUE_RECT_Y1 (PADDING)
#define HUE_RECT_WIDTH (12)
#define HUE_RECT_HEIGHT (SATURATION_LIGHTNESS_RECT_HEIGHT)
#define HUE_RECT_X2 (HUE_RECT_X1 + HUE_RECT_WIDTH)
#define HUE_RECT_Y2 (HUE_RECT_Y1 + HUE_RECT_HEIGHT)

typedef struct Color Color;

struct Color {
	float r, g, b;
	float h, s, l;
};

struct Picker {
	int width;
	int height;
	bool selecting;
	bool visible;
	uint32_t *px;
	xcb_connection_t *conn;
	xcb_window_t win;
	xcb_gcontext_t gc;
	xcb_image_t *img;
	PickerOnColorChangeHandler occ;
	Color color;
};

/***********************************************/
/*      __hue2rgb, __color_compute_hsl &       */
/*      __color_compute_rgb are modified       */
/*      versions of functions taken from       */
/*   https://gist.github.com/ciembor/1494530   */
/*     License: 'do what you want license'     */
/*   thanks ciembor (Maciej Ciemborowicz) :)!  */
/***********************************************/
static float
__hue2rgb(float p, float q, float t)
{
	if (t < 0) t += 1;
	if (t > 1) t -= 1;
	if (t < 1./6) return p + (q - p) * 6 * t;
	if (t < 1./2) return q;
	if (t < 2./3) return p + (q - p) * (2./3 - t) * 6;
	return p;
}

static void
__color_compute_hsl(Color *color)
{
	float d;
	float max, min;
	float r, g, b;

	r = color->r / 255;
	g = color->g / 255;
	b = color->b / 255;

	max = MAX(MAX(r,g),b);
	min = MIN(MIN(r,g),b);

	color->h = color->s = color->l = (max + min) / 2;

	if (max == min) {
		color->h = color->s = 0; // achromatic
	} else {
		d = max - min;
		color->s = (color->l > 0.5) ? d / (2 - max - min) : d / (max + min);
		if (max == r)
			color->h = (g - b) / d + (g < b ? 6 : 0);
		else if (max == g)
			color->h = (b - r) / d + 2;
		else if (max == b)
			color->h = (r - g) / d + 4;
		color->h /= 6;
	}
}

static void
__color_compute_rgb(Color *color)
{
	float q, p;
	float h, s, l;

	h = color->h;
	s = color->s;
	l = color->l;

	if (0 == s) {
		color->r = color->g = color->b = l * 255; // achromatic
	} else {
		q = l < 0.5 ? l * (1 + s) : l + s - l * s;
		p = 2 * l - q;
		color->r = __hue2rgb(p, q, h + 1./3) * 255;
		color->g = __hue2rgb(p, q, h) * 255;
		color->b = __hue2rgb(p, q, h - 1./3) * 255;
	}
}

static Color
__color_make_hsl(float h, float s, float l)
{
	Color color;
	color.h = h;
	color.s = s;
	color.l = l;
	__color_compute_rgb(&color);
	return color;
}

static Color
__color_make_rgb(float r, float g, float b)
{
	Color color;
	color.r = r;
	color.g = g;
	color.b = b;
	__color_compute_hsl(&color);
	return color;
}

static uint32_t
__color_to_uint32(const Color color)
{
	return (((int)(color.r) << 16) |
			((int)(color.g) <<  8) |
			((int)(color.b) <<  0));
}

extern Picker *
picker_new(xcb_connection_t *conn, xcb_window_t parent_win, PickerOnColorChangeHandler occ)
{
	int w, h;
	Picker *picker;
	size_t szpx;
	xcb_screen_t *scr;
	uint8_t depth;

	w = HUE_RECT_X2 + PADDING;
	h = HUE_RECT_Y2 + PADDING;

	assert(conn != NULL);

	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	assert(scr != NULL);

	szpx = w * h * sizeof(uint32_t);
	picker = xcalloc(1, sizeof(Picker));
	depth = scr->root_depth;

	picker->visible = false;
	picker->selecting = false;
	picker->width = w;
	picker->height = h;
	picker->occ = occ;

	picker->conn = conn;
	picker->gc = xcb_generate_id(conn);
	picker->win = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, depth, picker->win, parent_win, 0, 0,
		w, h, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		scr->root_visual, XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION
		}}
	);

	xcb_create_gc(conn, picker->gc, picker->win, 0, NULL);

	picker->px = xcalloc(w * h, sizeof(uint32_t));

	picker->img = xcb_image_create_native(conn, w, h,
			XCB_IMAGE_FORMAT_Z_PIXMAP, depth, picker->px,
			szpx, (uint8_t *)(picker->px));

	return picker;
}

static void
__picker_draw(Picker *picker)
{
	int dx, dy;
	Color col;

	memset(picker->px, 0, picker->width * picker->height * sizeof(uint32_t));

	for (dy = PADDING - 5; dy < PADDING - 1; ++dy)
		picker->px[(dy)*picker->width+(PADDING+(int)((1-picker->color.s)*SATURATION_LIGHTNESS_RECT_WIDTH))] = 0xffffff;

	for (dx = PADDING - 5; dx < PADDING - 1; ++dx)
		picker->px[(PADDING + (int)((1-picker->color.l)*SATURATION_LIGHTNESS_RECT_HEIGHT))*picker->width+dx] = 0xffffff;

	for (dx = 1; dx < PADDING - 5; ++dx)
		picker->px[(PADDING + (int)(picker->color.h*HUE_RECT_HEIGHT))*picker->width+HUE_RECT_X2+dx] = 0xffffff;

	for (dy = 0; dy < SATURATION_LIGHTNESS_RECT_HEIGHT; dy++) {
		for (dx = 0; dx < SATURATION_LIGHTNESS_RECT_WIDTH; dx++) {
			col = __color_make_hsl(picker->color.h, 1-(float)dx/SATURATION_LIGHTNESS_RECT_WIDTH, 1-(float)dy/SATURATION_LIGHTNESS_RECT_HEIGHT);
			picker->px[(PADDING+dy)*picker->width+PADDING+dx] = __color_to_uint32(col);
		}
	}

	for (dy = 0; dy < HUE_RECT_HEIGHT; dy++) {
		col = __color_make_hsl(((float)(dy))/HUE_RECT_HEIGHT, 1.0, 0.5);
		for (dx = 0; dx < HUE_RECT_WIDTH; dx++) {
			picker->px[(PADDING+dy)*picker->width+HUE_RECT_X1+dx] = __color_to_uint32(col);
		}
	}

	xcb_image_put(picker->conn, picker->win, picker->gc, picker->img, 0, 0, 0);
	xcb_flush(picker->conn);
}

static void
__picker_select_at(Picker *picker, int x, int y)
{
	if (x >= SATURATION_LIGHTNESS_RECT_X1 && x < SATURATION_LIGHTNESS_RECT_X2 &&
			y >= SATURATION_LIGHTNESS_RECT_Y1 && y < SATURATION_LIGHTNESS_RECT_Y2) {
		picker->color.s = 1-((float)(x-PADDING)/SATURATION_LIGHTNESS_RECT_WIDTH);
		picker->color.l = 1-((float)(y-PADDING)/SATURATION_LIGHTNESS_RECT_HEIGHT);
	}

	if (x >= HUE_RECT_X1 && x < HUE_RECT_X2 &&
			y >= HUE_RECT_Y1 && y < HUE_RECT_Y2) {
		picker->color.h = ((float)(y-PADDING)/HUE_RECT_HEIGHT);
	}

	__color_compute_rgb(&picker->color);

	picker->occ(picker, __color_to_uint32(picker->color));
	__picker_draw(picker);
}

static bool
__h_picker_expose(Picker *picker, const xcb_expose_event_t *ev)
{
	if (ev->window != picker->win)
		return false;
	__picker_draw(picker);
	return true;
}

static bool
__h_picker_button_press(Picker *picker, const xcb_button_press_event_t *ev)
{
	if (ev->event != picker->win)
		return false;
	switch (ev->detail) {
	case XCB_BUTTON_INDEX_1:
		picker->selecting = true;
		__picker_select_at(picker, ev->event_x, ev->event_y);
		break;
	case XCB_BUTTON_INDEX_2:
	case XCB_BUTTON_INDEX_3:
		picker_hide(picker);
		break;
	}
	return true;
}

static bool
__h_picker_button_release(Picker *picker, const xcb_button_release_event_t *ev)
{
	if (ev->event != picker->win)
		return false;
	if (ev->detail == XCB_BUTTON_INDEX_1)
		picker->selecting = false;
	return true;
}

static bool
__h_picker_motion_notify(Picker *picker, const xcb_motion_notify_event_t *ev)
{
	if (ev->event != picker->win)
		return false;
	if (picker->selecting)
		__picker_select_at(picker, ev->event_x, ev->event_y);
	return true;
}

extern bool
picker_try_process_event(Picker *picker, const xcb_generic_event_t *ge)
{
	switch (ge->response_type & ~0x80) {
	case XCB_EXPOSE:           return __h_picker_expose(picker, (void *)(ge));
	case XCB_BUTTON_PRESS:     return __h_picker_button_press(picker, (void *)(ge));
	case XCB_BUTTON_RELEASE:   return __h_picker_button_release(picker, (void *)(ge));
	case XCB_MOTION_NOTIFY:    return __h_picker_motion_notify(picker, (void *)(ge));
	default: return false;
	}
}

extern bool
picker_is_visible(const Picker *picker)
{
	return picker->visible;
}

extern void
picker_show(Picker *picker, int x, int y)
{
	xcb_configure_window(
		picker->conn, picker->win,
		XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y,
		(const uint32_t []) { x, y }
	);

	picker->visible = true;
	xcb_map_window(picker->conn, picker->win);
	xcb_flush(picker->conn);
}

extern void
picker_hide(Picker *picker)
{
	picker->selecting = false;
	picker->visible = false;
	xcb_unmap_window(picker->conn, picker->win);
	xcb_flush(picker->conn);
}

extern void
picker_set(Picker *picker, uint32_t color)
{
	float r, g, b;

	r = (color >> 16) & 0xff;
	g = (color >> 8) & 0xff;
	b = (color >> 0) & 0xff;

	picker->color = __color_make_rgb(r, g, b);
	__picker_draw(picker);
}

extern void
picker_destroy(Picker *picker)
{
	xcb_free_gc(picker->conn, picker->gc);
	xcb_image_destroy(picker->img);
	xcb_destroy_window(picker->conn, picker->win);
	free(picker);
}
