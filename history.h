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

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct HistoryAtomicAction HistoryAtomicAction;
typedef struct HistoryUserAction HistoryUserAction;
typedef struct History History;

struct HistoryAtomicAction {
	HistoryAtomicAction *next;
	int x, y;
	uint32_t color;
	int size;
};

struct HistoryUserAction {
	HistoryUserAction *prev;
	HistoryAtomicAction *aa;
	HistoryUserAction *next;
};

struct History {
	HistoryUserAction *root;
	HistoryUserAction *current;
};

extern History *
history_new(void);

extern HistoryUserAction *
history_user_action_new(void);

extern HistoryAtomicAction *
history_atomic_action_new(int x, int y, uint32_t color, int size);

extern void
history_user_action_push_atomic(HistoryUserAction *hua,
		HistoryAtomicAction *haa);

extern void
history_do(History *hist, HistoryUserAction *hua);

extern bool
history_undo(History *hist);

extern bool
history_redo(History *hist);

extern void
history_destroy(History *hist);
