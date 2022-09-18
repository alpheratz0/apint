# Copyright (C) 2022 <alpheratz99@protonmail.com>
# This program is free software.

VERSION   = 0.2.0

CC        = cc
CFLAGS    = -std=c99 -pedantic -Wall -Wextra -Os -DVERSION=\"$(VERSION)\"
LDLIBS    = -lxcb -lxcb-shm -lxcb-keysyms -lpng -lm
LDFLAGS   = -s

PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man
