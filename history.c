/*
	Copyright (C) 2023 <alpheratz99@protonmail.com>

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

#include <stdbool.h>
#include <stdlib.h>

#include "utils.h"
#include "history.h"

static void
__history_atomic_action_destroy(HistoryAtomicAction *haa)
{
	free(haa);
}

static void
__history_atomic_action_list_destroy(HistoryAtomicAction *list)
{
	HistoryAtomicAction *tmp;

	while (NULL != list) {
		tmp = list->next;
		__history_atomic_action_destroy(list);
		list = tmp;
	}
}

static void
__history_user_action_destroy(HistoryUserAction *hua)
{
	__history_atomic_action_list_destroy(hua->aa);
	free(hua);
}

static void
__history_user_action_list_destroy(HistoryUserAction *list)
{
	HistoryUserAction *tmp;

	while (NULL != list) {
		tmp = list->next;
		__history_user_action_destroy(list);
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
	hua = xmalloc(sizeof(HistoryUserAction));
	hua->next = NULL;
	hua->prev = NULL;
	hua->aa = NULL;
	return hua;
}

extern HistoryAtomicAction *
history_atomic_action_new(int x, int y, uint32_t color, int size)
{
	HistoryAtomicAction *haa;
	haa = xmalloc(sizeof(HistoryAtomicAction));
	haa->x = x;
	haa->y = y;
	haa->color = color;
	haa->size = size;
	haa->next = NULL;
	return haa;
}

extern void
history_user_action_push_atomic(HistoryUserAction *hua,
		HistoryAtomicAction *haa)
{
	HistoryAtomicAction *last;

	// check if there is no atomic actions
	// created yet
	if (NULL == hua->aa) {
		hua->aa = haa;
		return;
	}

	// get last node
	for (last = hua->aa; last->next; last = last->next)
		;

	last->next = haa;
}

extern void
history_do(History *hist, HistoryUserAction *hua)
{
	// destroy redo history
	__history_user_action_list_destroy(hist->current->next);

	// link
	hist->current->next = hua;
	hua->prev = hist->current;

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
history_destroy(History *hist)
{
	__history_user_action_list_destroy(hist->root);
	free(hist);
}
