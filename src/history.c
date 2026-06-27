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

#include <stdbool.h>
#include <stdlib.h>

#include "log.h"
#include "utils.h"
#include "history.h"

static void
__history_user_action_list_destroy(HistoryUserAction *list)
{
	HistoryUserAction *tmp;

	while (NULL != list) {
		tmp = list->next;
		history_user_action_destroy(list);
		list = tmp;
	}
}

extern History *
history_new(void)
{
	History *hist;
	hist = xmalloc(sizeof(History));
	hist->root = history_user_action_new();
	hist->current = hist->root;
	return hist;
}

extern HistoryUserAction *
history_user_action_new(void)
{
	HistoryUserAction *hua;
	hua = xcalloc(1, sizeof(HistoryUserAction));
	hua->type = HISTORY_STROKE;
	return hua;
}

extern void
history_user_action_push_point(HistoryUserAction *hua, int x, int y)
{
	if (hua->type != HISTORY_STROKE)
		die("history_user_action_push_point: action is not a stroke");

	if (hua->stroke.npoints >= hua->stroke.cap_points) {
		hua->stroke.cap_points = hua->stroke.cap_points
				? hua->stroke.cap_points * 2 : 32;
		hua->stroke.points = xrealloc(hua->stroke.points,
				hua->stroke.cap_points * sizeof(HistoryPoint));
	}

	hua->stroke.points[hua->stroke.npoints].x = x;
	hua->stroke.points[hua->stroke.npoints].y = y;
	hua->stroke.npoints++;
}

extern void
history_do(History *hist, HistoryUserAction *hua)
{
	// destroy redo history
	__history_user_action_list_destroy(hist->current->next);

	// link
	hist->current->next = hua;
	hua->prev = hist->current;
	hua->next = NULL;

	// update position in history
	hist->current = hua;
}

extern bool
history_undo(History *hist)
{
	if (NULL == hist->current->prev)
		return false;
	hist->current = hist->current->prev;
	return true;
}

extern bool
history_redo(History *hist)
{
	if (NULL == hist->current->next)
		return false;
	hist->current = hist->current->next;
	return true;
}

extern void
history_user_action_destroy(HistoryUserAction *hua)
{
	/* only the stroke variant owns heap memory */
	if (hua->type == HISTORY_STROKE)
		free(hua->stroke.points);
	free(hua);
}

extern void
history_destroy(History *hist)
{
	__history_user_action_list_destroy(hist->root);
	free(hist);
}
