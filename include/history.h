/*
	Copyright (C) 2023-2026 <alpheratz99@protonmail.com>

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

#include <stdbool.h>
#include <stdint.h>

typedef struct HistoryUserAction HistoryUserAction;
typedef struct History History;

/*
 * Each user action is stored as a single, high level description of the
 * operation (its type and parameters), not as the list of pixels/dabs it
 * produces. The canvas is rebuilt by replaying these operations in order.
 */
typedef enum {
	HISTORY_STROKE,     /* freehand path (uses points) */
	HISTORY_LINE,       /* uses x0,y0,x1,y1 */
	HISTORY_RECTANGLE,  /* uses x0,y0,x1,y1,fill */
	HISTORY_ELLIPSE,    /* uses x0,y0,x1,y1,fill */
	HISTORY_TRIANGLE,   /* uses x0,y0,x1,y1,fill */
	HISTORY_FILL        /* flood fill seeded at x0,y0 */
} HistoryActionType;

typedef struct {
	int x, y;
} HistoryPoint;

struct HistoryUserAction {
	HistoryUserAction *prev;
	HistoryUserAction *next;

	HistoryActionType type;
	uint32_t color;
	int size;

	union {
		/* HISTORY_STROKE */
		struct {
			HistoryPoint *points;
			int npoints;
			int cap_points;
		} stroke;

		/* HISTORY_LINE / HISTORY_RECTANGLE / HISTORY_ELLIPSE / HISTORY_TRIANGLE */
		struct {
			int x0, y0, x1, y1;
			bool fill;
		} shape;

		/* HISTORY_FILL */
		struct {
			int x, y;
		} bucket;
	};
};

struct History {
	HistoryUserAction *root;
	HistoryUserAction *current;
};

extern History *
history_new(void);

extern HistoryUserAction *
history_user_action_new(void);

extern void
history_user_action_push_point(HistoryUserAction *hua, int x, int y);

extern void
history_do(History *hist, HistoryUserAction *hua);

extern bool
history_undo(History *hist);

extern bool
history_redo(History *hist);

extern void
history_user_action_destroy(HistoryUserAction *hua);

extern void
history_destroy(History *hist);
