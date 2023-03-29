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

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

#include "utils.h"
#include "notify.h"

extern void
notify_send(const char *title, const char *body)
{
	pid_t pid;
	int status;

	if ((pid = fork()) < 0)
		die("fork failed");

	if (pid == 0) {
		execl("/bin/notify-send", "notify-send", title, body, (char *)(NULL));
		warn("exec() notify-send failed");
		exit(127);
	} else {
		if (waitpid(pid, &status, 0) < 0)
			die("waitpid");
	}
}
