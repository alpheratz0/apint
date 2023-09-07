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

#include <png.h>
#include <errno.h>
#include <setjmp.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>
#include <xcb/shm.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <stdint.h>

#include "log.h"
#include "color.h"
#include "canvas.h"
#include "utils.h"

#define SHMAT_INVALID_MEM ((void *)(-1))
#define XIMAGE_MAX_SIZE (16*1024*1024)

typedef struct {
	float x;
	float y;
} vec2f_t;

struct Canvas {
	xcb_connection_t *conn;
	xcb_window_t win;
	xcb_gcontext_t gc;
	xcb_point_t damage[2];
	vec2f_t pos;
	int width, height;
	int viewport_width;
	int viewport_height;
	uint32_t *px_raw;
	uint32_t *px_visual;
	uint32_t *px_snapshot;
	int shm;
	union {
		struct {
			int id;
			xcb_shm_seg_t seg;
			xcb_pixmap_t pixmap;
		} shm;
		xcb_image_t *image;
	} x;
};

/**
 * Check if the mit shm extension is supported.
 * We want to use this extension because transfering
 * the image data through unix sockets is much slower
 * than directly passing it through shared memory.
*/
static int
__x_check_mit_shm_extension(xcb_connection_t *conn)
{
	xcb_generic_error_t *error;
	xcb_shm_query_version_cookie_t cookie;
	xcb_shm_query_version_reply_t *reply;
	int supported;

	cookie = xcb_shm_query_version(conn);
	reply = xcb_shm_query_version_reply(conn, cookie, &error);
	supported = !error && reply && reply->shared_pixmaps;

	free(error); free(reply);

	return supported;
}

static int
__canvas_is_damaged(Canvas *c)
{
	if (c->damage[0].x == -1 || c->damage[1].x == -1 ||
			c->damage[0].y == -1 || c->damage[1].y == -1) {
		return 0;
	}
	return 1;
}

/**
 * A damaged pixel needs to recalculate its visual
 * appareance before rendering the canvas.
*/
static void
__canvas_damage(Canvas *c, int x, int y)
{
	if (!__canvas_is_damaged(c)) {
		c->damage[0].x = c->damage[1].x = x;
		c->damage[0].y = c->damage[1].y = y;
		return;
	}

	c->damage[0].x = MIN(c->damage[0].x, x);
	c->damage[0].y = MIN(c->damage[0].y, y);
	c->damage[1].x = MAX(c->damage[1].x, x);
	c->damage[1].y = MAX(c->damage[1].y, y);
}

static void
__canvas_damage_full(Canvas *c)
{
	__canvas_damage(c, 0, 0);
	__canvas_damage(c, c->width-1, c->height-1);
}

static void
__canvas_damage_process(Canvas *c)
{
	// simulated alpha bg
	uint32_t color, sa;
	int x, y;

	if (!__canvas_is_damaged(c)) {
		return;
	}

	for (y = c->damage[0].y; y <= c->damage[1].y; ++y) {
		for (x = c->damage[0].x; x <= c->damage[1].x; ++x) {
			color = c->px_raw[y * c->width + x];
			sa = ((x / 9 + y / 9) % 2 == 0 ? 0x646464 : 0x909090);
			c->px_visual[y * c->width + x] = color_mix(sa, color, ALPHA(color));
		}
	}

	c->damage[0].x = c->damage[1].x = -1;
	c->damage[0].y = c->damage[1].y = -1;
}

static void
__canvas_keep_visible(Canvas *c)
{
	c->pos.x = CLAMP(c->pos.x, -c->width, c->viewport_width);
	c->pos.y = CLAMP(c->pos.y, -c->height, c->viewport_height);
}

static void
__canvas_fill(Canvas *c, uint32_t color)
{
	int x, y;

	for (y = 0; y < c->height; ++y)
		for (x = 0; x < c->width; ++x)
			c->px_raw[y*c->width+x] = color;

	__canvas_damage_full(c);
}

static void
__canvas_take_snapshot(Canvas *c)
{
	free(c->px_snapshot);
	c->px_snapshot = xmalloc(4*c->width*c->height);
	memcpy(c->px_snapshot, c->px_raw, 4*c->width*c->height);
}

static void
__canvas_restore_snapshot(Canvas *c)
{
	if (NULL != c->px_snapshot) {
		memcpy(c->px_raw, c->px_snapshot, 4*c->width*c->height);
		__canvas_damage_full(c);
	}
}

extern Canvas *
canvas_new(xcb_connection_t *conn, xcb_window_t win, int w, int h, uint32_t bg)
{
	xcb_screen_t *screen;
	Canvas *c;

	c = xcalloc(1, sizeof(Canvas));

	c->conn = conn;
	c->win = win;
	c->viewport_width = c->width = w;
	c->viewport_height = c->height = h;
	c->px_raw = xmalloc(w*h*4);
	c->damage[0].x = c->damage[0].y = -1;
	c->damage[1].x = c->damage[1].y = -1;
	c->gc = xcb_generate_id(conn);
	c->shm = __x_check_mit_shm_extension(conn);

	screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	if (NULL == screen)
		die("can't get default screen");

	xcb_create_gc(conn, c->gc, win, 0, NULL);

	if (c->shm) {
		c->x.shm.seg    = xcb_generate_id(conn);
		c->x.shm.pixmap = xcb_generate_id(conn);
		c->x.shm.id     = shmget(IPC_PRIVATE, w*h*4, IPC_CREAT|0600);

		if (c->x.shm.id < 0)
			die("shmget:");

		c->px_visual = shmat(c->x.shm.id, NULL, 0);

		if (SHMAT_INVALID_MEM == c->px_visual) {
			shmctl(c->x.shm.id, IPC_RMID, NULL);
			die("shmat:");
		}

		xcb_shm_attach(conn, c->x.shm.seg, c->x.shm.id, 0);
		shmctl(c->x.shm.id, IPC_RMID, NULL);

		xcb_shm_create_pixmap(
			conn, c->x.shm.pixmap, win, w, h,
			screen->root_depth,
			c->x.shm.seg, 0
		);
	} else {
		if (w*h*4 > XIMAGE_MAX_SIZE)
			die("xcb_image_t can't handle images larger than 16MB");

		c->px_visual = xmalloc(w*h*4);

		c->x.image = xcb_image_create_native(
			conn, w, h, XCB_IMAGE_FORMAT_Z_PIXMAP,
			screen->root_depth, c->px_visual,
			w*h*4, (uint8_t *)(c->px_visual)
		);
	}

	__canvas_fill(c, bg);
	__canvas_take_snapshot(c);
	__canvas_damage_full(c);

	return c;
}

extern Canvas *
canvas_load(xcb_connection_t *conn, xcb_window_t win, const char *path)
{
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte **rows, bit_depth;
	Canvas *c;
	int16_t x, y;

	if (NULL == (fp = fopen(path, "rb")))
		die("failed to open file %s:", path);

	if (NULL == (png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
					NULL, NULL, NULL)))
		die("png_create_read_struct failed");

	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");

	png_init_io(png, fp);
	png_read_info(png, pnginfo);
	c = canvas_new(conn, win, png_get_image_width(png, pnginfo),
			png_get_image_height(png, pnginfo), 0);

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
		break;
	}

	png_read_update_info(png, pnginfo);

	rows = png_malloc(png, sizeof(png_byte *) * c->height);

	for (y = 0; y < c->height; ++y)
		rows[y] = png_malloc(png, png_get_rowbytes(png, pnginfo));

	png_read_image(png, rows);

	for (y = 0; y < c->height; ++y) {
		for (x = 0; x < c->width; ++x)
			c->px_raw[y*c->width+x] = color_pack_from_arr(&rows[y][x*4]);
		png_free(png, rows[y]);
	}

	__canvas_take_snapshot(c);
	__canvas_damage_full(c);

	png_free(png, rows);
	png_read_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_info_struct(png, &pnginfo);
	png_destroy_read_struct(&png, NULL, NULL);
	fclose(fp);

	return c;
}

extern void
canvas_save(const Canvas *c, const char *path)
{
	int x, y;
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte *row;

	if (NULL == (fp = fopen(path, "wb")))
		die("failed to open file %s:", path);

	if (NULL == (png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
					NULL, NULL, NULL)))
		die("png_create_write_struct failed");

	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");

	png_init_io(png, fp);
	png_set_IHDR(png, pnginfo, c->width, c->height, 8,
		PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE
	);

	png_write_info(png, pnginfo);
	png_set_compression_level(png, 3);

	row = png_malloc(png, c->width * 4);

	for (y = 0; y < c->height; ++y) {
		for (x = 0; x < c->width; ++x)
			color_unpack_to_arr(c->px_raw[y*c->width+x], &row[x*4]);
		png_write_row(png, row);
	}

	png_free(png, row);
	png_write_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_info_struct(png, &pnginfo);
	png_destroy_write_struct(&png, NULL);
	fclose(fp);
}

extern void
canvas_move_relative(Canvas *c, int offx, int offy)
{
	c->pos.x += offx;
	c->pos.y += offy;

	__canvas_keep_visible(c);
}

extern void
canvas_set_viewport(Canvas *c, int vw, int vh)
{
	/* keep distance ratio from top/left/right/bottom */
	c->pos.x += ((float)vw - c->viewport_width) / 2;
	c->pos.y += ((float)vh - c->viewport_height) / 2;

	c->viewport_width = vw;
	c->viewport_height = vh;

	__canvas_keep_visible(c);
}

extern void
canvas_render(Canvas *c)
{
	__canvas_damage_process(c);

	if (c->pos.y > 0)
		xcb_clear_area(c->conn, 0, c->win, 0, 0, c->viewport_width, c->pos.y);

	if (c->pos.y + c->height < c->viewport_height)
		xcb_clear_area(c->conn, 0, c->win, 0, c->pos.y + c->height,
				c->viewport_width, c->viewport_height - (c->pos.y + c->height));

	if (c->pos.x > 0)
		xcb_clear_area(c->conn, 0, c->win, 0, 0, c->pos.x, c->viewport_height);

	if (c->pos.x + c->width < c->viewport_width)
		xcb_clear_area(c->conn, 0, c->win, c->pos.x + c->width, 0,
				c->viewport_width - (c->pos.x + c->width), c->viewport_height);

	if (c->shm) {
		xcb_copy_area(c->conn, c->x.shm.pixmap, c->win,
				c->gc, 0, 0, c->pos.x, c->pos.y, c->width, c->height);
	} else {
		xcb_image_put(c->conn, c->win, c->gc,
				c->x.image, c->pos.x, c->pos.y, 0);
	}

	xcb_flush(c->conn);
}

extern void
canvas_set_pixel(Canvas *c, int x, int y, uint32_t color)
{
	if (x >= 0 && x < c->width && y >= 0 && y < c->height) {
		c->px_raw[y*c->width+x] = color;
		__canvas_damage(c, x, y);
	}
}

extern int
canvas_get_pixel(Canvas *c, int x, int y, uint32_t *color)
{
	if (x >= 0 && x < c->width && y >= 0 && y < c->height) {
		*color = c->px_raw[y*c->width+x];
		return 1;
	}
	return 0;
}

extern void
canvas_viewport_to_canvas_pos(Canvas *c, int x, int y, int *out_x, int *out_y)
{
	*out_x = x - c->pos.x;
	*out_y = y - c->pos.y;
}

extern void
canvas_clear(Canvas *c)
{
	__canvas_restore_snapshot(c);
}

extern void
canvas_free(Canvas *c)
{
	xcb_free_gc(c->conn, c->gc);

	if (c->shm) {
		shmctl(c->x.shm.id, IPC_RMID, NULL);
		xcb_shm_detach(c->conn, c->x.shm.seg);
		shmdt(c->px_visual);
		xcb_free_pixmap(c->conn, c->x.shm.pixmap);
	} else {
		xcb_image_destroy(c->x.image);
	}

	free(c->px_raw);
	free(c->px_snapshot);
	free(c);
}
