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
#include <assert.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>
#include <xcb/shm.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <stdint.h>

#include "canvas.h"
#include "utils.h"

struct Canvas {
	struct {
		int x;
		int y;
	} pos;

	int viewport_width;
	int viewport_height;

	int width;
	int height;
	uint32_t *px;
	uint32_t *orig_px;

	/* X11 */
	xcb_connection_t *conn;
	xcb_window_t win;
	int shm;
	xcb_gcontext_t gc;
	union {
		struct {
			int id;
			xcb_shm_seg_t seg;
			xcb_pixmap_t pixmap;
		} shm;
		xcb_image_t *image;
	} x;
};

static int
__x_check_mit_shm_extension(xcb_connection_t *conn)
{
	xcb_generic_error_t *error;
	xcb_shm_query_version_cookie_t cookie;
	xcb_shm_query_version_reply_t *reply;

	cookie = xcb_shm_query_version(conn);
	reply = xcb_shm_query_version_reply(conn, cookie, &error);

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
__canvas_keep_visible(Canvas *c)
{
	if (c->pos.x < -c->width)
		c->pos.x = -c->width;

	if (c->pos.y < -c->height)
		c->pos.y = -c->height;

	if (c->pos.x > c->viewport_width)
		c->pos.x = c->viewport_width;

	if (c->pos.y > c->viewport_height)
		c->pos.y = c->viewport_height;
}

static inline uint32_t *
__canvas_get_pixel_ptr(Canvas *c, int x, int y)
{
	x -= c->pos.x;
	y -= c->pos.y;

	if (x >= 0 && x < c->width
			&& y >= 0 && y < c->height) {
		return &c->px[y*c->width+x];
	}

	return NULL;
}

static void
__canvas_set_orig_px_to_current_state(Canvas *c)
{
	size_t szpx;
	szpx = sizeof(uint32_t) * c->width * c->height;

	free(c->orig_px);
	c->orig_px = xmalloc(szpx);

	memcpy(c->orig_px, c->px, szpx);
}

extern Canvas *
canvas_new(xcb_connection_t *conn, xcb_window_t win, int w, int h)
{
	Canvas *c;
	size_t szpx;
	xcb_screen_t *scr;
	uint8_t depth;

	assert(conn != NULL);
	assert(w > 0);
	assert(h > 0);

	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
	assert(scr != NULL);

	szpx = w * h * sizeof(uint32_t);
	c = xcalloc(1, sizeof(Canvas));
	depth = scr->root_depth;

	c->conn = conn;
	c->win = win;
	c->width = w;
	c->height = h;
	c->orig_px = NULL;
	c->viewport_width = 0;
	c->viewport_height = 0;

	c->gc = xcb_generate_id(conn);
	xcb_create_gc(conn, c->gc, win, 0, NULL);

	if (__x_check_mit_shm_extension(conn)) {
		c->shm = 1;

		c->x.shm.seg = xcb_generate_id(conn);
		c->x.shm.pixmap = xcb_generate_id(conn);
		c->x.shm.id = shmget(IPC_PRIVATE, szpx, IPC_CREAT | 0600);

		if (c->x.shm.id < 0)
			die("shmget failed");

		c->px = shmat(c->x.shm.id, NULL, 0);

		if (c->px == (void *) -1) {
			shmctl(c->x.shm.id, IPC_RMID, NULL);
			die("shmat failed");
		}

		xcb_shm_attach(conn, c->x.shm.seg, c->x.shm.id, 0);
		shmctl(c->x.shm.id, IPC_RMID, NULL);

		xcb_shm_create_pixmap(conn, c->x.shm.pixmap, win, w, h,
				depth, c->x.shm.seg, 0);
	} else {
		c->shm = 0;

		// FIXME: split source image into
		//        multiple xcb_image_t objects
		if (szpx > 16*1024*1024 /* 16mb */)
			die("image too big for one xcb_image_t");

		c->px = xmalloc(szpx);

		c->x.image = xcb_image_create_native(conn, w, h,
				XCB_IMAGE_FORMAT_Z_PIXMAP, depth, c->px,
				szpx, (uint8_t *)(c->px));
	}

	memset(c->px, 255, szpx);

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
	c = canvas_new(conn, win, png_get_image_width(png, pnginfo),
			png_get_image_height(png, pnginfo));

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

	rows = png_malloc(png, sizeof(png_byte *) * c->height);

	for (y = 0; y < c->height; ++y)
		rows[y] = png_malloc(png, png_get_rowbytes(png, pnginfo));

	png_read_image(png, rows);

	for (y = 0; y < c->height; ++y) {
		for (x = 0; x < c->width; ++x) {
			if (rows[y][x*4+3] == 0) {
				c->px[y*c->width+x] = (x/9+y/9)%2 == 0 ? 0xe6e6e6 : 0xffffff;
			} else {
				c->px[y*c->width+x] = 0xff << 24 |
								rows[y][x*4+0] << 16 |
								rows[y][x*4+1] << 8 |
								rows[y][x*4+2];
			}
		}
		png_free(png, rows[y]);
	}

	__canvas_set_orig_px_to_current_state(c);

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
		die("fopen failed: %s", strerror(errno));

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
		for (x = 0; x < c->width; ++x) {
			row[x*4+0] = (c->px[y*c->width+x] >> 16) & 0xff;
			row[x*4+1] = (c->px[y*c->width+x] >>  8) & 0xff;
			row[x*4+2] = (c->px[y*c->width+x] >>  0) & 0xff;
			row[x*4+3] = (c->px[y*c->width+x] >> 24) & 0xff;
		}
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
canvas_destroy(Canvas *c)
{
	xcb_free_gc(c->conn, c->gc);

	if (c->shm) {
		shmctl(c->x.shm.id, IPC_RMID, NULL);
		xcb_shm_detach(c->conn, c->x.shm.seg);
		shmdt(c->px);
		xcb_free_pixmap(c->conn, c->x.shm.pixmap);
	} else {
		xcb_image_destroy(c->x.image);
	}

	free(c->orig_px);
	free(c);
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
	if (c->viewport_width == 0 || c->viewport_height == 0) {
		c->pos.x = (vw - c->width) / 2;
		c->pos.y = (vh - c->height) / 2;
	} else {
		c->pos.x += (vw - c->viewport_width) / 2;
		c->pos.y += (vh - c->viewport_height) / 2;
	}

	c->viewport_width = vw;
	c->viewport_height = vh;

	__canvas_keep_visible(c);
}

extern void
canvas_render(Canvas *c)
{
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
	uint32_t *px;
	if (NULL != (px = __canvas_get_pixel_ptr(c, x, y))) {
		*px = (color & 0xff000000) == 0 ?
			((x/9+y/9)%2 == 0 ? 0xe6e6e6 : 0xffffff) :
			color;
	}
}

extern int
canvas_get_pixel(Canvas *c, int x, int y, uint32_t *color)
{
	uint32_t *px;
	if (NULL != (px = __canvas_get_pixel_ptr(c, x, y))) {
		*color = *px;
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
canvas_canvas_to_viewport_pos(Canvas *c, int x, int y, int *out_x, int *out_y)
{
	*out_x = x + c->pos.x;
	*out_y = y + c->pos.y;
}

extern void
canvas_clear(Canvas *c)
{
	size_t szpx;
	szpx = sizeof(uint32_t) * c->width * c->height;
	if (c->orig_px) {
		memcpy(c->px, c->orig_px, szpx);
	} else {
		memset(c->px, 255, szpx);
	}
}
