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
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>
#include "utils.h"
#include "log.h"

extern const char *
enotnull(const char *str, const char *name)
{
	if (NULL == str)
		die("%s cannot be null", name);
	return str;
}

extern void *
xmalloc(size_t size)
{
	void *ptr;
	if (NULL == (ptr = malloc(size)))
		die("OOM");
	return ptr;
}

extern void *
xcalloc(size_t nmemb, size_t size)
{
	void *ptr;
	if (NULL == (ptr = calloc(nmemb, size)))
		die("OOM");
	return ptr;
}

extern char *
xstrdup(const char *str)
{
	size_t len;
	char *res;

	if (NULL == str)
		return NULL;

	len = strlen(str);
	res = xmalloc(len + 1);
	memcpy(res, str, len);
	res[len] = '\0';

	return res;
}

extern char *
xprompt(const char *prompt)
{
	int fds[2];
	pid_t pid;
	ssize_t count;
	ssize_t total_read_count;
	ssize_t left_to_read;
	char *output, *nlpos;
	int status;

	if (pipe(fds) < 0)
		die("pipe:");

	if ((pid = fork()) < 0)
		die("fork:");

	if (pid == 0) {
		while ((dup2(fds[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
		freopen("/dev/null", "r", stdin);
		close(fds[1]);
		close(fds[0]);

		execlp("dmenu", "dmenu", "-p", prompt, (char*)(NULL));
		execlp("rofi", "rofi", "-dmenu", "-i", "-p", prompt, "-hint-welcome",
				"", "-hint-result", "", (char*)(NULL));

		// TODO: add more fallbacks

		_exit(127);
	}

	close(fds[1]);

	total_read_count = 0;
	left_to_read = 255;
	output = xcalloc(left_to_read + 1, sizeof(char));

	while (1) {
		count = read(fds[0], &output[total_read_count], left_to_read);
		if (count == -1) {
			if (errno == EINTR)
				continue;
			die("read:");
		} else if (count == 0) {
			break;
		} else {
			total_read_count += count;
			left_to_read -= count;
		}
	}

	close(fds[0]);

	if (waitpid(pid, &status, 0) < 0)
		die("waitpid:");

	if (!WIFEXITED(status) || WEXITSTATUS(status) > 0) {
		free(output);
		return NULL;
	}

	nlpos = strchr(output, '\n');

	if (nlpos)
		*nlpos = '\0';

	return output;
}

extern bool
path_is_writeable(const char *path)
{
	FILE *fp;

	if (NULL == (fp = fopen(path, "w")))
		return false;

	fclose(fp);

	return true;
}

extern char *
path_expand(const char *path)
{
	char *home, *res;

	if (NULL == path)
		return NULL;

	if (path[0] != '~')
		return xstrdup(path);

	if (NULL == (home = getenv("HOME")))
		return NULL;

	res = xmalloc(strlen(home)+strlen(path));
	sprintf(res, "%s%s", home, path+1);

	return res;
}

extern void
size_parse(const char *str, int *width, int *height)
{
	char *end = NULL;
	*width = strtol(str, &end, 10);
	if (!end || *end != 'x' || end == str) {
		*width = *height = 0;
		return;
	}
	*height = atoi(end+1);
}
