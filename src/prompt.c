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

#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "utils.h"
#include "prompt.h"

extern char *
prompt_read(const char *prompt)
{
	int fd[2];
	pid_t pid;
	int status;
	int read_count, total_read_count, left_to_read;
	char *output, *nlpos;

	if (pipe(fd) < 0)
		die("pipe failed");

	if ((pid = fork()) < 0)
		die("fork failed");

	if (pid == 0) {
		freopen("/dev/null", "r", stdin);
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		execlp("dmenu", "dmenu", "-p", prompt, (char *)(NULL));
		warn("execlp() dmenu failed");
		execlp("rofi", "rofi", "-dmenu", "-i", "-p", prompt, "-hint-welcome", "", "-hint-result", "", (char *)(NULL));
		warn("execlp() rofi failed");
		_exit(127);
	} else {
		close(fd[1]);

		read_count = -1;
		total_read_count = 0;
		left_to_read = 255;
		output = xcalloc(left_to_read + 1, sizeof(char));

		while (read_count != 0) {
			if ((read_count = read(fd[0], output + total_read_count, left_to_read)) < 0)
				die("read");
			total_read_count += read_count;
			left_to_read -= read_count;
		}

		if (waitpid(pid, &status, 0) < 0)
			die("waitpid");

		if (!WIFEXITED(status) || WEXITSTATUS(status) > 0) {
			free(output);
			return NULL;
		}

		nlpos = strchr(output, '\n');

		if (nlpos)
			*nlpos = '\0';

		return output;
	}

	return NULL;
}
