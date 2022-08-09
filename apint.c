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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xcb/shm.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define ARRLEN(arr)                        (sizeof(arr)/sizeof(arr[0]))
#define UNUSED                             __attribute__((unused))

#define DEFAULT_CANVAS_WIDTH               (800)
#define DEFAULT_CANVAS_HEIGHT              (600)

#define RED                                (0xff0000)
#define GREEN                              (0xff00)
#define BLUE                               (0xff)
#define YELLOW                             (RED|GREEN)
#define VIOLET                             (RED|BLUE)
#define TURQUOISE                          (GREEN|BLUE)
#define WHITE                              (RED|GREEN|BLUE)
#define BLACK                              (0)

enum {
	DM_NONE,
	DM_PAINT,
	DM_ERASE
};

enum {
	SM_WINDOWED,
	SM_FULLSCREEN
};

struct brush {
	uint32_t color;
	int8_t size;
};

static xcb_connection_t *conn;
static xcb_window_t window;
static xcb_screen_t *screen;
static xcb_gcontext_t gc;
static xcb_key_symbols_t *ksyms;
static xcb_shm_seg_t shmseg;
static int shmid;
static xcb_pixmap_t pixmap;
static uint8_t draw_mode, startup_mode;
static int16_t canvas_width, canvas_height;
static uint32_t *pixels;

static const uint32_t palette[] = { RED, GREEN, BLUE, YELLOW, VIOLET, TURQUOISE, WHITE, BLACK };
static struct brush paint_brush = { RED, 10 };
static struct brush erase_brush = { WHITE, 30 };

static void
die(const char *err)
{
	fprintf(stderr, "apint: %s\n", err);
	exit(1);
}

static void
dief(const char *fmt, ...)
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
	if (NULL == str) {
		dief("%s cannot be null", name);
	}

	return str;
}

static xcb_atom_t
get_atom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	error = NULL;
	cookie = xcb_intern_atom(conn, 1, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, &error);

	if (NULL != error) {
		dief("xcb_intern_atom failed with error code: %d",
				(int)(error->error_code));
	}

	atom = reply->atom;
	free(reply);

	return atom;
}

static void
get_window_size(int16_t *width, int16_t *height)
{
	xcb_generic_error_t *error;
	xcb_get_geometry_cookie_t cookie;
	xcb_get_geometry_reply_t *reply;

	error = NULL;
	cookie = xcb_get_geometry(conn, window);
	reply = xcb_get_geometry_reply(conn, cookie, &error);

	if (NULL != error) {
		dief("xcb_get_geometry failed with error code: %d",
				(int)(error->error_code));
	}

	*width = reply->width;
	*height = reply->height;

	free(reply);
}

static void
check_shm_extension(void)
{
	xcb_generic_error_t *error;
	xcb_shm_query_version_cookie_t cookie;
	xcb_shm_query_version_reply_t *reply;

	error = NULL;
	cookie = xcb_shm_query_version(conn);
	reply = xcb_shm_query_version_reply(conn, cookie, &error);

	if (NULL != error) {
		dief("xcb_shm_query_version failed with error code: %d",
				(int)(error->error_code));
	}

	if (reply->shared_pixmaps == 0) {
		die("shm extension doesn't support shared pixmaps");
	}

	free(reply);
}

static void
create_window(void)
{
	if (xcb_connection_has_error(conn = xcb_connect(NULL, NULL))) {
		die("can't open display");
	}

	if (NULL == (screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data)) {
		xcb_disconnect(conn);
		die("can't get default screen");
	}

	ksyms = xcb_key_symbols_alloc(conn);
	window = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	xcb_create_window(
		conn, screen->root_depth, window, screen->root, 0, 0,
		800, 600, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(const uint32_t[]) {
			screen->black_pixel,
			XCB_EVENT_MASK_EXPOSURE |
			XCB_EVENT_MASK_KEY_PRESS |
			XCB_EVENT_MASK_BUTTON_PRESS |
			XCB_EVENT_MASK_BUTTON_RELEASE |
			XCB_EVENT_MASK_POINTER_MOTION |
			XCB_EVENT_MASK_KEYMAP_STATE
		}
	);

	/* set WM_NAME */
	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
		sizeof("apint") - 1, "apint"
	);

	/* set WM_CLASS */
	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
		sizeof("apint") * 2, "apint\0apint\0"
	);

	/* add WM_DELETE_WINDOW to WM_PROTOCOLS */
	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1,
		(const xcb_atom_t[]) { get_atom("WM_DELETE_WINDOW") }
	);

	/* remove any transparency */
	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("_NET_WM_WINDOW_OPACITY"), XCB_ATOM_CARDINAL, 32, 1,
		(const uint8_t[]) { 0xff, 0xff, 0xff, 0xff }
	);

	if (startup_mode == SM_FULLSCREEN) {
		xcb_change_property(
			conn, XCB_PROP_MODE_REPLACE, window,
			get_atom("_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 1,
			(const xcb_atom_t[]) { get_atom("_NET_WM_STATE_FULLSCREEN") }
		);
	}

	xcb_create_gc(conn, gc, window, 0, 0);

	xcb_map_window(conn, window);
	xcb_flush(conn);
}

static void
create_canvas(int16_t width, int16_t height)
{
	xcb_void_cookie_t cookie;
	xcb_generic_error_t *error;

	canvas_width = width;
	canvas_height = height;

	shmseg = xcb_generate_id(conn);
	pixmap = xcb_generate_id(conn);

	check_shm_extension();

	shmid = shmget(
		IPC_PRIVATE, width * height * sizeof(uint32_t),
		IPC_CREAT | 0600
	);

	if (shmid < 0) {
		dief("shmget failed: %s", strerror(errno));
	}

	pixels = shmat(shmid, NULL, 0);

	if ((void *)(-1) == pixels) {
		shmctl(shmid, IPC_RMID, NULL);
		dief("shmat failed: %s", strerror(errno));
	}

	memset(pixels, 255, width * height * sizeof(uint32_t));

	cookie = xcb_shm_attach_checked(conn, shmseg, shmid, 0);
	error = xcb_request_check(conn, cookie);

	if (NULL != error) {
		shmctl(shmid, IPC_RMID, NULL);
		dief("xcb_shm_attach failed with error code: %d",
				(int)(error->error_code));
	}

	xcb_shm_create_pixmap(
		conn, pixmap, window, width, height,
		screen->root_depth, shmseg, 0
	);

	xcb_flush(conn);
}

static void
load_canvas(const char *path)
{
	FILE *fp;
	int16_t x, y;
	uint8_t pix[3];
	uint8_t hdr[128];
	size_t hdrlen;
	uint8_t nlc;

	if (NULL == (fp = fopen(path, "rb"))) {
		dief("failed to open file %s: %s", path, strerror(errno));
	}

	for (hdrlen = 0, nlc = 0; nlc != 2 && hdrlen < ARRLEN(hdr); ++hdrlen) {
		if (fread(&hdr[hdrlen], 1, 1, fp) != 1) {
			die("invalid file format");
		}

		if (hdr[hdrlen] == '\n') ++nlc;
	}

	hdr[hdrlen] = 0;

	if (sscanf((char *)(hdr), "P6\n%hd %hd 255\n", &canvas_width, &canvas_height) != 2) {
		die("invalid file format");
	}

	if (canvas_width <= 0 || canvas_height <= 0) {
		die("invalid file format");
	}

	create_canvas(canvas_width, canvas_height);

	for (y = 0; y < canvas_height; y++) {
		for (x = 0; x < canvas_width; x++) {
			if (fread(pix, sizeof(pix[0]), ARRLEN(pix), fp) != ARRLEN(pix)) {
				die("invalid file format");
			}
			pixels[y*canvas_width+x] = pix[0] << 16 | pix[1] << 8 | pix[2];
		}
	}

	fclose(fp);
}

static void
destroy_window(void)
{
	shmctl(shmid, IPC_RMID, NULL);
	xcb_shm_detach(conn, shmseg);
	shmdt(pixels);
	xcb_key_symbols_free(ksyms);
	xcb_free_gc(conn, gc);
	xcb_free_pixmap(conn, pixmap);
	xcb_disconnect(conn);
}

static void
render_scene(void)
{
	int16_t width, height;

	get_window_size(&width, &height);

	xcb_copy_area(
		conn, pixmap, window, gc, 0, 0, (width - canvas_width) / 2,
		(height - canvas_height) / 2, canvas_width, canvas_height
	);

	xcb_flush(conn);
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

static void
add_point(int16_t x, int16_t y, struct brush brush)
{
	int8_t dx, dy;
	int16_t mapx, mapy;
	int16_t width, height;
	double distance;

	get_window_size(&width, &height);

	x -= (width - canvas_width) / 2;
	y -= (height - canvas_height) / 2;

	for (dx = -brush.size; dx < brush.size; dx++) {
		mapx = x + dx;
		if (mapx < 0 || mapx >= canvas_width) continue;
		for (dy = -brush.size; dy < brush.size; dy++) {
			mapy = y + dy;
			if (mapy < 0 || mapy >= canvas_height) continue;
			distance = sqrt(dx*dx+dy*dy);
			if (distance < brush.size) {
				pixels[mapy*canvas_width+mapx] = color_lerp(
					brush.color, pixels[mapy*canvas_width+mapx],
					distance/brush.size
				);
			}
		}
	}

	render_scene();
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
	render_scene();
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if (key >= XKB_KEY_1 && key < (XKB_KEY_1 + ARRLEN(palette))) {
		paint_brush.color = palette[key - XKB_KEY_1];
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			if (draw_mode == DM_NONE) {
				draw_mode = DM_PAINT;
				add_point(ev->event_x, ev->event_y, paint_brush);
			}
			break;
		case XCB_BUTTON_INDEX_3:
			if (draw_mode == DM_NONE) {
				draw_mode = DM_ERASE;
				add_point(ev->event_x, ev->event_y, erase_brush);
			}
			break;
		case XCB_BUTTON_INDEX_4:
			if (paint_brush.size < 50) {
				paint_brush.size += 1;
			}
			break;
		case XCB_BUTTON_INDEX_5:
			if (paint_brush.size > 2) {
				paint_brush.size -= 1;
			}
			break;
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	switch (draw_mode) {
		case DM_PAINT:
			add_point(ev->event_x, ev->event_y, paint_brush);
			break;
		case DM_ERASE:
			add_point(ev->event_x, ev->event_y, erase_brush);
			break;
	}
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
		case XCB_BUTTON_INDEX_3:
			draw_mode = DM_NONE;
			break;
	}
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
      xcb_refresh_keyboard_mapping(ksyms, ev);
}

int
main(int argc, char **argv)
{
	const char *loadpath = NULL;
	xcb_generic_event_t *ev;

	while (++argv, --argc > 0) {
		if (!strcmp(*argv, "-h")) usage();
		else if (!strcmp(*argv, "-v")) version();
		else if (!strcmp(*argv, "-f")) startup_mode = SM_FULLSCREEN;
		else if (!strcmp(*argv, "-l")) --argc, loadpath = enotnull(*++argv, "path");
		else if (**argv == '-') dief("invalid option %s", *argv);
		else dief("unexpected argument: %s", *argv);
	}

	create_window();

	if (NULL == loadpath) create_canvas(DEFAULT_CANVAS_WIDTH, DEFAULT_CANVAS_HEIGHT);
	else load_canvas(loadpath);

	while ((ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE:
				h_client_message((xcb_client_message_event_t *)(ev));
				break;
			case XCB_EXPOSE:
				h_expose((xcb_expose_event_t *)(ev));
				break;
			case XCB_KEY_PRESS:
				h_key_press((xcb_key_press_event_t *)(ev));
				break;
			case XCB_BUTTON_PRESS:
				h_button_press((xcb_button_press_event_t *)(ev));
				break;
			case XCB_MOTION_NOTIFY:
				h_motion_notify((xcb_motion_notify_event_t *)(ev));
				break;
			case XCB_BUTTON_RELEASE:
				h_button_release((xcb_button_release_event_t *)(ev));
				break;
			case XCB_MAPPING_NOTIFY:
				h_mapping_notify((xcb_mapping_notify_event_t *)(ev));
				break;
		}

		free(ev);
	}

	destroy_window();

	return 0;
}
