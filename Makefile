.POSIX:
.PHONY: all clean install uninstall dist

include config.mk

OBJ=\
	src/apint.o \
	src/canvas.o \
	src/color.o \
	src/picker.o \
	src/utils.o \
	src/history.o \
	src/log.o

all: apint

apint: $(OBJ)
	$(CC) $(LDFLAGS) -o apint $(OBJ) $(LDLIBS)

clean:
	rm -f apint $(OBJ) apint-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f apint $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/apint
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f apint.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/apint.1

dist: clean
	mkdir -p apint-$(VERSION)
	cp -R COPYING config.mk Makefile README apint.1 src include \
		apint-$(VERSION)
	tar -cf apint-$(VERSION).tar apint-$(VERSION)
	gzip apint-$(VERSION).tar
	rm -rf apint-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/apint
	rm -f $(DESTDIR)$(MANPREFIX)/man1/apint.1
