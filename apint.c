/*
	Copyright (C) 2022 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License version 2 as published by the
	Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along with
	this program; if not, write to the Free Software Foundation, Inc., 59 Temple
	Place, Suite 330, Boston, MA 02111-1307 USA

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

/* TODO: cleanup dragging functionality */
/* TODO: cleanup dragging functionality */
/* TODO: cleanup dragging functionality */

#include <png.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <xcb/xcb.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define UNUSED __attribute__((unused))

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static xcb_gcontext_t gc;
static xcb_image_t *image;
static xcb_key_symbols_t *ksyms;
static xcb_point_t dbp, dcp;
static int start_in_fullscreen, painting, dragging;
static int32_t wwidth, wheight, cwidth, cheight;
static int32_t previous_brush_size, brush_size;
static uint32_t *px, *cpx, color;
static uint32_t pi, palette[] = {
	0xff0000, 0x00ff00, 0x0000ff, 0xffff00,
	0xff00ff, 0x00ffff, 0x000000, 0xffffff
};


static void
die(const char *fmt, ...)
{
	va_list args;

	fputs("apint: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

static const char *
enotnull(const char *str, const char *name)
{
	if (NULL == str)
		die("%s cannot be null", name);
	return str;
}

static xcb_atom_t
get_atom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, &error);

	if (NULL != error)
		die("xcb_intern_atom failed with error code: %hhu", error->error_code);

	atom = reply->atom;
	free(reply);

	return atom;
}

static void
create_window(void)
{
	if (xcb_connection_has_error(conn = xcb_connect(NULL, NULL)))
		die("can't open display");

	if (NULL == (screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data))
		die("can't get default screen");

	wwidth = wheight = 600;

	if (NULL == (px = calloc(wwidth * wheight, sizeof(uint32_t))))
		die("error while calling malloc, no memory available");

	ksyms = xcb_key_symbols_alloc(conn);
	window = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, screen->root_depth, window, screen->root, 0, 0,
		wwidth, wheight, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual, XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_KEY_RELEASE |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);

	xcb_create_gc(conn, gc, window, 0, NULL);

	image = xcb_image_create_native(
		conn, wwidth, wheight, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth,
		px, sizeof(uint32_t) * wwidth * wheight, (uint8_t *)(px)
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME,
		XCB_ATOM_STRING, 8, sizeof("apint") - 1, "apint"
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, sizeof("apint") * 2, "apint\0apint\0"
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1,
		(const xcb_atom_t []) { get_atom("WM_DELETE_WINDOW") }
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("_NET_WM_WINDOW_OPACITY"), XCB_ATOM_CARDINAL, 32, 1,
		(const uint8_t []) { 0xff, 0xff, 0xff, 0xff }
	);

	if (start_in_fullscreen) {
		xcb_change_property(
			conn, XCB_PROP_MODE_REPLACE, window,
			get_atom("_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 1,
			(const xcb_atom_t []) { get_atom("_NET_WM_STATE_FULLSCREEN") }
		);
	}

	xcb_map_window(conn, window);
	xcb_flush(conn);
}

static void
destroy_window(void)
{
	xcb_free_gc(conn, gc);
	xcb_key_symbols_free(ksyms);
	xcb_image_destroy(image);
	xcb_disconnect(conn);
}

static void
create_canvas(int32_t width, int32_t height)
{
	cwidth = width;
	cheight = height;
	cpx = malloc(sizeof(uint32_t) * cwidth * cheight);

	if (NULL == cpx)
		die("error while calling malloc, no memory available");

	memset(cpx, 255, sizeof(uint32_t) * cwidth * cheight);
}

static void
destroy_canvas(void)
{
	free(cpx);
}

static void
load_canvas(const char *path)
{
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte *row, bit_depth;
	int16_t x, y;

	if (NULL == (fp = fopen(path, "rb")))
		die("failed to open file %s: %s", path, strerror(errno));

	if (NULL == (png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)))
		die("png_create_read_struct failed");

	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");

	png_init_io(png, fp);
	png_read_info(png, pnginfo);
	create_canvas(png_get_image_width(png, pnginfo), png_get_image_height(png, pnginfo));

	bit_depth = png_get_bit_depth(png, pnginfo);

	if (bit_depth == 16)
		png_set_strip_16(png);

	if (png_get_valid(png, pnginfo, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);

	switch (png_get_color_type(png, pnginfo)) {
		case PNG_COLOR_TYPE_RGB:
			png_set_filler(png, 0xff, PNG_FILLER_AFTER);
			break;
		case PNG_COLOR_TYPE_PALETTE:
			png_set_palette_to_rgb(png);
			png_set_filler(png, 0xff, PNG_FILLER_AFTER);
			break;
		case PNG_COLOR_TYPE_GRAY:
			if (bit_depth < 8)
				png_set_expand_gray_1_2_4_to_8(png);
			png_set_filler(png, 0xff, PNG_FILLER_AFTER);
			png_set_gray_to_rgb(png);
			break;
	}

	png_read_update_info(png, pnginfo);

	row = malloc(png_get_rowbytes(png, pnginfo));

	for (y = 0; y < cheight; ++y) {
		png_read_row(png, row, NULL);
		for (x = 0; x < cwidth; ++x) {
			cpx[y*cwidth+x] = row[x*4+0] << 16 |
				row[x*4+1] << 8 |
				row[x*4+2] << 0;
		}
	}

	png_read_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_read_struct(&png, NULL, NULL);
	fclose(fp);
	free(row);
}

static void
prepare_render(void)
{
	int32_t x, y, ox, oy;

	memset(px, 0, sizeof(uint32_t) * wwidth * wheight);

	ox = (dcp.x - dbp.x) + (wwidth - cwidth) / 2;
	oy = (dcp.y - dbp.y) + (wheight - cheight) / 2;

	for (y = 0; y < cheight; ++y) {
		if ((y+oy) < 0 || (y+oy) >= wheight)
			continue;
		for (x = 0; x < cwidth; ++x) {
			if ((x+ox) < 0 || (x+ox) >= wwidth)
				continue;
			px[(y+oy)*wwidth+(x+ox)] = cpx[y*cwidth+x];
		}
	}
}

static void
set_color(uint32_t c)
{
	color = c;
}

static void
set_brush_size(int32_t bs)
{
	previous_brush_size = brush_size;
	brush_size = bs;
}

static void
drag_begin(int16_t x, int16_t y)
{
	dragging = 1;
	dbp.x = dbp.x - dcp.x + x;
	dbp.y = dbp.y - dcp.y + y;
}

static void
drag_update(int32_t x, int32_t y)
{
	dcp.x = x;
	dcp.y = y;

	prepare_render();
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
	xcb_flush(conn);
}

static void
drag_end(void)
{
	dragging = 0;
}

static inline uint8_t
blerp(uint8_t from, uint8_t to, double v)
{
	return from + ((to - from) * v);
}

static uint32_t
color_lerp(uint32_t from, uint32_t to, double v)
{
	uint8_t r, g, b;

	v = v > 1 ? 1 : v < 0 ? 0 : v;
	r = blerp((from >> 16) & 0xff, (to >> 16) & 0xff, v);
	g = blerp((from >> 8) & 0xff, (to >> 8) & 0xff, v);
	b = blerp(from & 0xff, to & 0xff, v);

	return (r << 16) | (g << 8) | b;
}

static int
window_coord_to_canvas_coord(int32_t wx, int32_t wy, int32_t *cx, int32_t *cy)
{
	*cx = (dbp.x - dcp.x) + wx - (wwidth - cwidth) / 2;
	*cy = (dbp.y - dcp.y) + wy - (wheight - cheight) / 2;

	return *cx >= 0 && *cx < cwidth && *cy >= 0 && *cy < cheight;
}

static void
add_point_to_canvas(int32_t x, int32_t y, uint32_t color, int32_t size)
{
	int32_t dx, dy;

	for (dy = -size; dy < size; ++dy) {
		if ((y+dy) < 0 || (y+dy) >= cheight)
			continue;
		for (dx = -size; dx < size; ++dx) {
			if ((x+dx) < 0 || (x+dx) >= cwidth)
				continue;
			if (dy*dy+dx*dx < size*size)
				cpx[(y+dy)*cwidth+(x+dx)] = color_lerp(color,
						cpx[(y+dy)*cwidth+(x+dx)], sqrt(dy*dy+dx*dx)/size);
		}
	}

	prepare_render();
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
	xcb_flush(conn);
}

static void
usage(void)
{
	puts("usage: apint [-fhv] [-l file]");
	exit(0);
}

static void
version(void)
{
	puts("apint version "VERSION);
	exit(0);
}

static void
h_client_message(xcb_client_message_event_t *ev)
{
	/* check if the wm sent a delete window message */
	/* https://www.x.org/docs/ICCCM/icccm.pdf */
	if (ev->data.data32[0] == get_atom("WM_DELETE_WINDOW")) {
		destroy_window();
		exit(0);
	}
}

static void
h_expose(UNUSED xcb_expose_event_t *ev)
{
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if (key == XKB_KEY_d) {
		set_color(0xffffff);
	} else if (key == XKB_KEY_s) {
		set_brush_size(10);
	} else if (key == XKB_KEY_b) {
		set_brush_size(40);
	} else if (key >= XKB_KEY_1 && key < (XKB_KEY_1 + sizeof(palette)/sizeof(palette[0]))) {
		set_color(palette[pi = (key - XKB_KEY_1)]);
	}
}

static void
h_key_release(xcb_key_release_event_t *ev)
{
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if (key == XKB_KEY_d) {
		set_color(palette[pi]);
	} else if (key == XKB_KEY_s || key == XKB_KEY_b) {
		set_brush_size(previous_brush_size);
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	int32_t x, y;

	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			if (window_coord_to_canvas_coord(ev->event_x, ev->event_y, &x, &y))
				painting = 1, add_point_to_canvas(x, y, color, brush_size);
			break;
		case XCB_BUTTON_INDEX_2:
			drag_begin(ev->event_x, ev->event_y);
			break;
		case XCB_BUTTON_INDEX_3:
			if (window_coord_to_canvas_coord(ev->event_x, ev->event_y, &x, &y))
				set_color(cpx[y*cwidth+x]);
			break;
		case XCB_BUTTON_INDEX_4:
			brush_size += 2;
			break;
		case XCB_BUTTON_INDEX_5:
			brush_size -= (brush_size > 10) ? 2 : 0;
			break;
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	int32_t x, y;

	if (painting && window_coord_to_canvas_coord(ev->event_x, ev->event_y, &x, &y)) {
		add_point_to_canvas(x, y, color, brush_size);
	} else if (dragging) {
		drag_update(ev->event_x, ev->event_y);
	}
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	if (ev->detail == XCB_BUTTON_INDEX_1) {
		painting = 0;
	} else if (ev->detail == XCB_BUTTON_INDEX_2) {
		drag_end();
	}
}

static void
h_configure_notify(xcb_configure_notify_event_t *ev)
{
	if (wwidth == ev->width && wheight == ev->height)
		return;

	xcb_image_destroy(image);

	wwidth = ev->width;
	wheight = ev->height;
	px = calloc(wwidth * wheight, sizeof(uint32_t));

	image = xcb_image_create_native(
		conn, wwidth, wheight, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth,
		px, sizeof(uint32_t) * wwidth * wheight, (uint8_t *)(px)
	);

	prepare_render();
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
	xcb_flush(conn);
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(ksyms, ev);
}

int
main(int argc, char **argv)
{
	const char *loadpath;
	xcb_generic_event_t *ev;

	loadpath = NULL;

	while (++argv, --argc > 0) {
		if ((*argv)[0] == '-' && (*argv)[1] != '\0' && (*argv)[2] == '\0') {
			switch ((*argv)[1]) {
				case 'h': usage(); break;
				case 'v': version(); break;
				case 'f': start_in_fullscreen = 1; break;
				case 'l': --argc; loadpath = enotnull(*++argv, "path"); break;
				default: die("invalid option %s", *argv); break;
			}
		} else {
			die("unexpected argument: %s", *argv);
		}
	}

	create_window();
	set_color(palette[0]);
	set_brush_size(15);

	if (NULL == loadpath) create_canvas(600, 600);
	else load_canvas(loadpath);

	while ((ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE:     h_client_message((void *)(ev)); break;
			case XCB_EXPOSE:             h_expose((void *)(ev)); break;
			case XCB_KEY_PRESS:          h_key_press((void *)(ev)); break;
			case XCB_KEY_RELEASE:        h_key_release((void *)(ev)); break;
			case XCB_BUTTON_PRESS:       h_button_press((void *)(ev)); break;
			case XCB_MOTION_NOTIFY:      h_motion_notify((void *)(ev)); break;
			case XCB_BUTTON_RELEASE:     h_button_release((void *)(ev)); break;
			case XCB_CONFIGURE_NOTIFY:   h_configure_notify((void *)(ev)); break;
			case XCB_MAPPING_NOTIFY:     h_mapping_notify((void *)(ev)); break;
		}

		free(ev);
	}

	destroy_window();
	destroy_canvas();

	return 0;
}
