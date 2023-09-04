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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <unistd.h>
#include "log.h"

static int
log_stderr(const char *s)
{
	fprintf(stderr, "apint: %s\n", s);
	return 0;
}

static int
log_notify_send(const char *s)
{
	pid_t pid;
	int status;

	if ((pid = fork()) < 0)
		return -1;

	if (pid == 0) {
		execlp("notify-send", "notify-send",
				"apint", s, (char *)(NULL));
		_exit(127);
	}

	if (waitpid(pid, &status, 0) < 0 ||
			(WIFEXITED(status) &&
				WEXITSTATUS(status) == 127))
		return -1;

	return 0;
}

static void
log_context_based(const char *s)
{
	if (isatty(STDOUT_FILENO)) {
		log_stderr(s);
	} else {
		log_notify_send(s);
	}
}

extern void
info(const char *fmt, ...)
{
	va_list args;
	char msg[256];

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);
	log_context_based(msg);
	va_end(args);
}

extern void
die(const char *fmt, ...)
{
	va_list args;
	char msg[256], msg_w_strerr[512];

	va_start(args, fmt);
	vsnprintf(msg, sizeof(msg), fmt, args);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':') {
		snprintf(msg_w_strerr, sizeof(msg_w_strerr),
				"%s %s", msg, strerror(errno));

		log_context_based(msg_w_strerr);
	} else {
		log_context_based(msg);
	}

	va_end(args);

	exit(1);
}
