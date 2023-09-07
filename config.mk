# Copyright (C) 2022-2023 <alpheratz99@protonmail.com>
# This program is free software.

VERSION=0.8.1
CC=cc
INCS=-I/usr/X11R6/include -Iinclude
CFLAGS=-std=c99 -pedantic -Wall -Wextra -Os $(INCS) -DVERSION=\"$(VERSION)\"
LDLIBS=-lxcb -lxcb-shm -lxcb-image -lxcb-keysyms -lxcb-cursor -lpng -lm
LDFLAGS=-L/usr/X11R6/lib -s
PREFIX=/usr/local
MANPREFIX=$(PREFIX)/share/man
