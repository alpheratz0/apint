/*
	Copyright (C) 2022-2023 <alpheratz99@protonmail.com>

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

#include <png.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/shm.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <saveas/saveas.h>

#define UNUSED __attribute__((unused))

typedef struct {
	int shm;
	int width;
	int height;
	uint32_t *px;
	xcb_gcontext_t gc;
	union {
		struct {
			int id;
			xcb_shm_seg_t seg;
			xcb_pixmap_t pixmap;
		} shm;
		xcb_image_t *image;
	} x;
} XCanvas;

typedef struct {
	xcb_connection_t *conn;
	xcb_screen_t *screen;
	xcb_window_t id;
	int32_t width;
	int32_t height;
	xcb_key_symbols_t *ksyms;
	xcb_cursor_context_t *cctx;
	xcb_cursor_t chand;
	xcb_cursor_t carrow;
} XWindow;

typedef struct {
	xcb_point_t p0;
	xcb_point_t p1;
} XOffset;

typedef struct {
	int fullscreen;
} ApintStartOptions;

typedef struct {
	uint32_t index;
	uint32_t colors[8];
} ApintPalette;

typedef struct {
	int painting;
	int dragging;
	XOffset drag_offset;
	int32_t brush_size;
	int32_t previous_brush_size;
	uint32_t color;
	uint32_t previous_color;
	ApintPalette palette;
	uint32_t *undo_buffer;
} ApintState;

static XWindow win;
static XCanvas canvas;
static ApintStartOptions start_options;
static ApintState state;

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

static void
stateinit(void)
{
	state.palette.colors[0] = 0xff0000;
	state.palette.colors[1] = 0x00ff00;
	state.palette.colors[2] = 0x0000ff;
	state.palette.colors[3] = 0xffff00;
	state.palette.colors[4] = 0xff00ff;
	state.palette.colors[5] = 0x00ffff;
	state.palette.colors[6] = 0x000000;
	state.palette.colors[7] = 0xffffff;
	state.brush_size = state.previous_brush_size = 15;
	state.color = state.previous_color = state.palette.colors[0];
}

static xcb_atom_t
xatom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;
	cookie = xcb_intern_atom(win.conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(win.conn, cookie, &error);
	if (NULL != error)
		die("xcb_intern_atom failed with error code: %hhu",
				error->error_code);
	atom = reply->atom;
	free(reply);
	return atom;
}

static int
xtestmitshm(void)
{
	xcb_generic_error_t *error;
	xcb_shm_query_version_cookie_t cookie;
	xcb_shm_query_version_reply_t *reply;
	cookie = xcb_shm_query_version(win.conn);
	reply = xcb_shm_query_version_reply(win.conn, cookie, &error);
	if (NULL != error) {
		if (NULL != reply)
			free(reply);
		free(error);
		return 0;
	}
	if (NULL != reply) {
		if (reply->shared_pixmaps == 0) {
			free(reply);
			return 0;
		}
		free(reply);
		return 1;
	}
	return 0;
}

static void
xwininit(void)
{
	win.conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(win.conn))
		die("can't open display");
	win.screen = xcb_setup_roots_iterator(xcb_get_setup(win.conn)).data;
	if (NULL == win.screen)
		die("can't get default screen");
	if (xcb_cursor_context_new(win.conn, win.screen, &win.cctx) != 0)
		die("can't create cursor context");
	win.chand = xcb_cursor_load_cursor(win.cctx, "fleur");
	win.carrow = xcb_cursor_load_cursor(win.cctx, "left_ptr");
	win.width = 800, win.height = 600;
	win.ksyms = xcb_key_symbols_alloc(win.conn);
	win.id = xcb_generate_id(win.conn);
	xcb_create_window_aux(
		win.conn, win.screen->root_depth, win.id, win.screen->root, 0, 0,
		win.width, win.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		win.screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.background_pixel = 0,
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_KEY_RELEASE |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);
	xcb_change_property(win.conn, XCB_PROP_MODE_REPLACE, win.id,
		xatom("_NET_WM_NAME"), xatom("UTF8_STRING"), 8,
		strlen("apint"), "apint");
	xcb_change_property(win.conn, XCB_PROP_MODE_REPLACE, win.id,
		XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
		strlen("apint\0apint\0"), "apint\0apint\0");
	xcb_change_property(win.conn, XCB_PROP_MODE_REPLACE, win.id,
		xatom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1,
		(const xcb_atom_t []) { xatom("WM_DELETE_WINDOW") });
	xcb_change_property(win.conn, XCB_PROP_MODE_REPLACE, win.id,
		xatom("_NET_WM_WINDOW_OPACITY"), XCB_ATOM_CARDINAL, 32, 1,
		(const uint8_t []) { 0xff, 0xff, 0xff, 0xff });
	if (start_options.fullscreen)
		xcb_change_property(win.conn, XCB_PROP_MODE_REPLACE, win.id,
			xatom("_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 1,
			(const xcb_atom_t []) { xatom("_NET_WM_STATE_FULLSCREEN") });
	xcb_map_window(win.conn, win.id);
	xcb_flush(win.conn);
}

static void
xwindestroy(void)
{
	xcb_free_cursor(win.conn, win.chand);
	xcb_free_cursor(win.conn, win.carrow);
	xcb_key_symbols_free(win.ksyms);
	xcb_cursor_context_free(win.cctx);
	xcb_disconnect(win.conn);
}

static void
xcanvasinit(int32_t width, int32_t height)
{
	canvas.width = width;
	canvas.height = height;
	canvas.shm = xtestmitshm();
	canvas.gc = xcb_generate_id(win.conn);
	xcb_create_gc(win.conn, canvas.gc, win.id, 0, NULL);
	if (canvas.shm) {
		canvas.x.shm.seg = xcb_generate_id(win.conn);
		canvas.x.shm.pixmap = xcb_generate_id(win.conn);
		canvas.x.shm.id = shmget(IPC_PRIVATE,
			width * height * sizeof(uint32_t), IPC_CREAT | 0600);
		if (canvas.x.shm.id < 0)
			die("shmget failed");
		canvas.px = shmat(canvas.x.shm.id, NULL, 0);
		if ((void *)(-1) == canvas.px) {
			shmctl(canvas.x.shm.id, IPC_RMID, NULL);
			die("shmat failed");
		}
		xcb_shm_attach(win.conn, canvas.x.shm.seg, canvas.x.shm.id, 0);
		shmctl(canvas.x.shm.id, IPC_RMID, NULL);
		memset(canvas.px, 255, sizeof(uint32_t) * width * height);
		xcb_shm_create_pixmap(win.conn, canvas.x.shm.pixmap, win.id,
			width, height, win.screen->root_depth, canvas.x.shm.seg, 0);
	} else {
		canvas.px = malloc(sizeof(uint32_t) * width * height);
		if (NULL == canvas.px)
			die("error while calling malloc, no memory available");
		memset(canvas.px, 255, sizeof(uint32_t) * width * height);
		canvas.x.image = xcb_image_create_native(
			win.conn, width, height, XCB_IMAGE_FORMAT_Z_PIXMAP,
			win.screen->root_depth, canvas.px,
			sizeof(uint32_t) * width * height, (uint8_t *)(canvas.px));
	}
}

static void
xcanvasdestroy(void)
{
	xcb_free_gc(win.conn, canvas.gc);
	if (canvas.shm) {
		shmctl(canvas.x.shm.id, IPC_RMID, NULL);
		xcb_shm_detach(win.conn, canvas.x.shm.seg);
		shmdt(canvas.px);
		xcb_free_pixmap(win.conn, canvas.x.shm.pixmap);
	} else {
		xcb_image_destroy(canvas.x.image);
	}
}

static void
xcanvasload(const char *path)
{
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte **rows, bit_depth;
	int16_t x, y;
	if (NULL == (fp = fopen(path, "rb")))
		die("failed to open file %s: %s", path, strerror(errno));
	if (NULL == (png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
					NULL, NULL, NULL)))
		die("png_create_read_struct failed");
	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");
	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");
	png_init_io(png, fp);
	png_read_info(png, pnginfo);
	xcanvasinit(png_get_image_width(png, pnginfo), png_get_image_height(png, pnginfo));
	bit_depth = png_get_bit_depth(png, pnginfo);
	png_set_interlace_handling(png);
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
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			png_set_gray_to_rgb(png);
	}
	png_read_update_info(png, pnginfo);
	rows = png_malloc(png, sizeof(png_byte *) * canvas.height);
	for (y = 0; y < canvas.height; ++y)
		rows[y] = png_malloc(png, png_get_rowbytes(png, pnginfo));
	png_read_image(png, rows);
	for (y = 0; y < canvas.height; ++y) {
		for (x = 0; x < canvas.width; ++x) {
			if (rows[y][x*4+3] == 0)
				canvas.px[y*canvas.width+x] = 0xffffff;
			else canvas.px[y*canvas.width+x] = rows[y][x*4+0] << 16 |
				rows[y][x*4+1] << 8 |
				rows[y][x*4+2];
		}
		png_free(png, rows[y]);
	}
	png_free(png, rows);
	png_read_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_read_struct(&png, NULL, NULL);
	fclose(fp);
}

static void
xcanvassave(const char *path)
{
	int x, y;
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte *row;
	if (NULL == (fp = fopen(path, "wb")))
		die("fopen failed: %s", strerror(errno));
	if (NULL == (png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
					NULL, NULL, NULL)))
		die("png_create_write_struct failed");
	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");
	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");
	png_init_io(png, fp);
	png_set_IHDR(png, pnginfo, canvas.width, canvas.height, 8,
			PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
	        PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
	png_write_info(png, pnginfo);
	png_set_compression_level(png, 3);
	row = malloc(canvas.width * 3);
	for (y = 0; y < canvas.height; ++y) {
		for (x = 0; x < canvas.width; ++x) {
			row[x*3+0] = (canvas.px[y*canvas.width+x] & 0xff0000) >> 16;
			row[x*3+1] = (canvas.px[y*canvas.width+x] & 0xff00) >> 8;
			row[x*3+2] = canvas.px[y*canvas.width+x] & 0xff;
		}
		png_write_row(png, row);
	}
	png_write_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_write_struct(&png, NULL);
	fclose(fp);
	free(row);
}

static void
xclearinvarea(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
	if (x < win.width && (x+width) > 0 && y < win.height && (y+height) > 0) {
		if (y > 0) xcb_clear_area(win.conn, 0, win.id, 0, 0, win.width, y);
		if (x > 0) xcb_clear_area(win.conn, 0, win.id, 0, 0, x, win.height);
		if ((y+height) < win.height) xcb_clear_area(win.conn, 0, win.id,
				0, y+height, win.width, win.height-y+height);
		if ((x+width) < win.width) xcb_clear_area(win.conn, 0, win.id,
				x+width, 0, win.width-x+width, win.height);
	} else {
		xcb_clear_area(win.conn, 0, win.id, 0, 0, win.width, win.height);
	}
}

static void
xswapbuffers(void)
{
	int32_t ox, oy;
	ox = (state.drag_offset.p1.x - state.drag_offset.p0.x) +
		(win.width - canvas.width) / 2;
	oy = (state.drag_offset.p1.y - state.drag_offset.p0.y) +
		(win.height - canvas.height) / 2;
	xclearinvarea(ox, oy, canvas.width, canvas.height);
	if (canvas.shm) {
		xcb_copy_area(win.conn, canvas.x.shm.pixmap, win.id,
			canvas.gc, 0, 0, ox, oy, canvas.width, canvas.height);
	} else {
		xcb_image_put(win.conn, win.id, canvas.gc,
			canvas.x.image, ox, oy, 0);
	}
	xcb_flush(win.conn);
}

static void
set_color(uint32_t c)
{
	state.previous_color = state.color;
	state.color = c;
}

static void
set_brush_size(int32_t bs)
{
	if (bs < 10) bs = 10;
	state.previous_brush_size = state.brush_size;
	state.brush_size = bs;
}

static void
undo(void)
{
	if (NULL == state.undo_buffer) return;
	memcpy(canvas.px, state.undo_buffer, canvas.width*canvas.height*sizeof(uint32_t));
	xswapbuffers();
}

static void
undohistorypush(void)
{
	if (state.undo_buffer == NULL)
		state.undo_buffer = malloc(canvas.width * canvas.height * sizeof(uint32_t));
	memcpy(state.undo_buffer, canvas.px, canvas.width*canvas.height*sizeof(uint32_t));
}

static void
undohistorydestroy(void)
{
	free(state.undo_buffer);
}

static void
drag_begin(int16_t x, int16_t y)
{
	state.dragging = 1;
	state.drag_offset.p0.x = state.drag_offset.p0.x - state.drag_offset.p1.x + x;
	state.drag_offset.p0.y = state.drag_offset.p0.y - state.drag_offset.p1.y + y;
	xcb_change_window_attributes(win.conn, win.id, XCB_CW_CURSOR, &win.chand);
	xcb_flush(win.conn);
}

static void
drag_update(int32_t x, int32_t y)
{
	state.drag_offset.p1.x = x;
	state.drag_offset.p1.y = y;
	xswapbuffers();
}

static void
drag_end(int32_t x, int32_t y)
{
	state.dragging = 0;
	state.drag_offset.p1.x = x;
	state.drag_offset.p1.y = y;
	xcb_change_window_attributes(win.conn, win.id, XCB_CW_CURSOR, &win.carrow);
	xcb_flush(win.conn);
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
xwin2canvascoord(int32_t wx, int32_t wy, int32_t *cx, int32_t *cy)
{
	*cx = (state.drag_offset.p0.x - state.drag_offset.p1.x) + wx - (win.width - canvas.width) / 2;
	*cy = (state.drag_offset.p0.y - state.drag_offset.p1.y) + wy - (win.height - canvas.height) / 2;
	return *cx >= 0 && *cx < canvas.width && *cy >= 0 && *cy < canvas.height;
}

static void
xcanvasaddpoint(int32_t x, int32_t y, uint32_t color, int32_t size)
{
	int32_t dx, dy;
	for (dy = -size; dy < size; ++dy) {
		if ((y+dy) < 0 || (y+dy) >= canvas.height)
			continue;
		for (dx = -size; dx < size; ++dx) {
			if ((x+dx) < 0 || (x+dx) >= canvas.width)
				continue;
			if (dy*dy+dx*dx < size*size)
				canvas.px[(y+dy)*canvas.width+(x+dx)] = color_lerp(color,
						canvas.px[(y+dy)*canvas.width+(x+dx)], sqrt(dy*dy+dx*dx)/size);
		}
	}
	xswapbuffers();
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
	if (ev->data.data32[0] == xatom("WM_DELETE_WINDOW")) {
		xcanvasdestroy();
		xwindestroy();
		undohistorydestroy();
		exit(0);
	}
}

static void
h_expose(UNUSED xcb_expose_event_t *ev)
{
	xswapbuffers();
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	const char *savepath;
	xcb_keysym_t key;
	key = xcb_key_symbols_get_keysym(win.ksyms, ev->detail, 0);
	if (key == XKB_KEY_d) {
		set_color(0xffffff);
	} else if (key == XKB_KEY_s) {
		if (ev->state & XCB_MOD_MASK_CONTROL) {
			if (saveas_show_popup(&savepath) == SAVEAS_STATUS_OK)
				xcanvassave(savepath);
		} else {
			set_brush_size(10);
		}
	} else if (key == XKB_KEY_b) {
		set_brush_size(40);
	} else if (key >= XKB_KEY_1 && key < (XKB_KEY_1 + sizeof(state.palette.colors)/sizeof(state.palette.colors[0]))) {
		set_color(state.palette.colors[state.palette.index = (key - XKB_KEY_1)]);
	} else if (key == XKB_KEY_z && ev->state & XCB_MOD_MASK_CONTROL) {
		undo();
	}
}

static void
h_key_release(xcb_key_release_event_t *ev)
{
	xcb_keysym_t key;
	key = xcb_key_symbols_get_keysym(win.ksyms, ev->detail, 0);
	if (key == XKB_KEY_d) {
		set_color(state.previous_color);
	} else if (key == XKB_KEY_s || key == XKB_KEY_b) {
		set_brush_size(state.previous_brush_size);
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	int32_t x, y;
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			if (!state.dragging && xwin2canvascoord(ev->event_x, ev->event_y, &x, &y)) {
				state.painting = 1;
				undohistorypush();
				xcanvasaddpoint(x, y, state.color, state.brush_size);
			}
			break;
		case XCB_BUTTON_INDEX_2:
			drag_begin(ev->event_x, ev->event_y);
			break;
		case XCB_BUTTON_INDEX_3:
			if (xwin2canvascoord(ev->event_x, ev->event_y, &x, &y))
				set_color(canvas.px[y*canvas.width+x]);
			break;
		case XCB_BUTTON_INDEX_4:
			set_brush_size(state.brush_size + 2);
			break;
		case XCB_BUTTON_INDEX_5:
			set_brush_size(state.brush_size - 2);
			break;
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	int32_t x, y;
	if (state.dragging) {
		drag_update(ev->event_x, ev->event_y);
	} else if (state.painting && xwin2canvascoord(ev->event_x, ev->event_y, &x, &y)) {
		xcanvasaddpoint(x, y, state.color, state.brush_size);
	}
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	if (ev->detail == XCB_BUTTON_INDEX_1) {
		state.painting = 0;
	} else if (ev->detail == XCB_BUTTON_INDEX_2) {
		drag_end(ev->event_x, ev->event_y);
	}
}

static void
h_configure_notify(xcb_configure_notify_event_t *ev)
{
	if (win.width == ev->width && win.height == ev->height)
		return;
	win.width = ev->width;
	win.height = ev->height;
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(win.ksyms, ev);
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
				case 'f': start_options.fullscreen = 1; break;
				case 'l': --argc; loadpath = enotnull(*++argv, "path"); break;
				default: die("invalid option %s", *argv); break;
			}
		} else {
			die("unexpected argument: %s", *argv);
		}
	}

	xwininit();
	stateinit();

	if (NULL == loadpath) xcanvasinit(800, 600);
	else xcanvasload(loadpath);

	while ((ev = xcb_wait_for_event(win.conn))) {
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

	xcanvasdestroy();
	xwindestroy();
	undohistorydestroy();

	return 0;
}
