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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "utils.h"

extern void
die(const char *fmt, ...)
{
	va_list args;

	fputs("apint: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

extern void
warn(const char *fmt, ...)
{
	va_list args;

	fputs("apint: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
}

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
