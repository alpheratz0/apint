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

*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/shm.h>

#define UNUSED                             __attribute__((unused))

#define KEY_1                              (10)
#define KEY_8                              (17)

#define CANVAS_WIDTH                       (640)
#define CANVAS_HEIGHT                      (480)

enum {
	DRAWMODE_NONE,
	DRAWMODE_PAINT,
	DRAWMODE_ERASE
};

struct brush {
	uint32_t color;
	int size;
};

static xcb_connection_t *conn;
static xcb_window_t window;
static xcb_screen_t *screen;
static xcb_gcontext_t gc;
static uint32_t shmseg;
static uint32_t shmid;
static xcb_pixmap_t pixmap;
static uint32_t *pixels;
static uint8_t drawmode;

static const uint32_t palette[8] = {
	0xff0000, 0x00ff00, 0x0000ff, 0xffff00,
	0x00ffff, 0x000000, 0xffffff, 0xcccccc
};

static struct brush paintb = {
	.color = palette[0],
	.size = 10
};

static struct brush eraseb = {
	.color = 0xffffff,
	.size = 30
};

static void
die(const char *err)
{
	fprintf(stderr, "apint: %s\n", err);
	exit(1);
}

static void
dief(const char *err, ...)
{
	va_list list;
	fputs("apint: ", stderr);
	va_start(list, err);
	vfprintf(stderr, err, list);
	va_end(list);
	fputc('\n', stderr);
	exit(1);
}

static xcb_atom_t
xatom(const char *name)
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
xsize(int16_t *width, int16_t *height)
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
	xcb_shm_query_version_reply_t *reply;
	xcb_shm_query_version_cookie_t cookie;
	xcb_generic_error_t *error;

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

	window = xcb_generate_id(conn);
	shmseg = xcb_generate_id(conn);
	pixmap = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	check_shm_extension();

	shmid = shmget(
		IPC_PRIVATE, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint32_t),
		IPC_CREAT | 0600
	);

	pixels = (uint32_t *)(shmat(shmid, NULL, 0));

	memset(pixels, 255, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint32_t));
	xcb_shm_attach(conn, shmseg, shmid, 0);
	shmctl(shmid, IPC_RMID, NULL);

	xcb_create_window(
		conn, screen->root_depth, window, screen->root, 0, 0,
		CANVAS_WIDTH, CANVAS_HEIGHT, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(const uint32_t[]) {
			screen->black_pixel,
			XCB_EVENT_MASK_EXPOSURE |
			XCB_EVENT_MASK_KEY_PRESS |
			XCB_EVENT_MASK_BUTTON_PRESS |
			XCB_EVENT_MASK_BUTTON_RELEASE |
			XCB_EVENT_MASK_POINTER_MOTION
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
		xatom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1,
		(const xcb_atom_t[]) { xatom("WM_DELETE_WINDOW") }
	);

	xcb_create_gc(conn, gc, window, 0, 0);

	xcb_shm_create_pixmap(
		conn, pixmap, window, CANVAS_WIDTH, CANVAS_HEIGHT,
		screen->root_depth, shmseg, 0
	);

	xcb_map_window(conn, window);
	xcb_flush(conn);
}

static void
destroy_window(void)
{
	xcb_shm_detach(conn, shmseg);
	shmdt(pixels);
	xcb_free_gc(conn, gc);
	xcb_free_pixmap(conn, pixmap);
	xcb_disconnect(conn);
}

static void
render_scene(void)
{
	int16_t width, height;

	xsize(&width, &height);

	xcb_copy_area(
		conn, pixmap, window, gc, 0, 0, (width - CANVAS_WIDTH) / 2,
		(height - CANVAS_HEIGHT) / 2, CANVAS_WIDTH, CANVAS_HEIGHT
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
add_point(int x, int y, struct brush brush)
{
	int dx, dy;
	int mapx, mapy;
	int16_t width, height;
	double distance;

	xsize(&width, &height);

	x -= (width - CANVAS_WIDTH) / 2;
	y -= (height - CANVAS_HEIGHT) / 2;

	for (dx = -brush.size; dx < brush.size; dx++) {
		mapx = x + dx;
		if (mapx < 0 || mapx >= CANVAS_WIDTH) continue;
		for (dy = -brush.size; dy < brush.size; dy++) {
			mapy = y + dy;
			if (mapy < 0 || mapy >= CANVAS_HEIGHT) continue;
			distance = sqrt(dx*dx+dy*dy);
			if (distance < brush.size) {
				pixels[mapy*CANVAS_WIDTH+mapx] = color_lerp(
					brush.color, pixels[mapy*CANVAS_WIDTH+mapx],
					distance/brush.size
				);
			}
		}
	}

	render_scene();
}

static int
match_opt(const char *in, const char *sh, const char *lo)
{
	return (strcmp(in, sh) == 0) || (strcmp(in, lo) == 0);
}

static inline void
print_opt(const char *sh, const char *lo, const char *desc)
{
	printf("%7s | %-25s %s\n", sh, lo, desc);
}

static void
usage(void)
{
	puts("Usage: apint [ -hv ]");
	puts("Options are:");
	print_opt("-h", "--help", "display this message and exit");
	print_opt("-v", "--version", "display the program version");
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
	if (ev->detail >= KEY_1 && ev->detail <= KEY_8) {
		paintb.color = palette[(int)(ev->detail) - KEY_1];
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	if (drawmode != DRAWMODE_NONE) {
		return;
	}

	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			drawmode = DRAWMODE_PAINT;
			add_point(ev->event_x, ev->event_y, paintb);
			break;
		case XCB_BUTTON_INDEX_3:
			drawmode = DRAWMODE_ERASE;
			add_point(ev->event_x, ev->event_y, eraseb);
			break;
		case XCB_BUTTON_INDEX_4:
			if (paintb.size < 50) {
				paintb.size += 1;
			}
			break;
		case XCB_BUTTON_INDEX_5:
			if (paintb.size > 2) {
				paintb.size -= 1;
			}
			break;
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	switch (drawmode) {
		case DRAWMODE_PAINT:
			add_point(ev->event_x, ev->event_y, paintb);
			break;
		case DRAWMODE_ERASE:
			add_point(ev->event_x, ev->event_y, eraseb);
			break;
	}
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
		case XCB_BUTTON_INDEX_3:
			drawmode = DRAWMODE_NONE;
			break;
	}
}

int
main(int argc, char **argv)
{
	xcb_generic_event_t *ev;

	if (++argv, --argc > 0) {
		if (match_opt(*argv, "-h", "--help")) usage();
		else if (match_opt(*argv, "-v", "--version")) version();
		else if (**argv == '-') dief("invalid option %s", *argv);
		else dief("unexpected argument: %s", *argv);
	}

	create_window();

	while((ev = xcb_wait_for_event(conn))) {
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
		}

		free(ev);
	}

	destroy_window();

	return 0;
}
