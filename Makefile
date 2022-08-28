.POSIX:
.PHONY: all clean install uninstall dist

VERSION = 0.1.0

CC      = cc
CFLAGS  = -std=c99 -pedantic -Wall -Wextra -Os -DVERSION=\"$(VERSION)\"
LDLIBS  = -lxcb -lxcb-shm -lxcb-keysyms -lm
LDFLAGS = -s

PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man

all: apint

apint: apint.o
	$(CC) $(LDFLAGS) -o apint apint.o $(LDLIBS)

clean:
	rm -f apint apint.o apint-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f apint $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/apint
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f apint.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/apint.1

dist: clean
	mkdir -p apint-$(VERSION)
	cp -R COPYING Makefile README apint.1 apint.c apint-$(VERSION)
	tar -cf apint-$(VERSION).tar apint-$(VERSION)
	gzip apint-$(VERSION).tar
	rm -rf apint-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/apint
	rm -f $(DESTDIR)$(MANPREFIX)/man1/apint.1
