.POSIX:
.PHONY: all clean install uninstall dist

include config.mk

all: apint

apint.o:    apint.c
canvas.o:   canvas.c
picker.o:   picker.c
utils.o:    utils.c
history.o:  history.c
prompt.o:   prompt.c

apint: apint.o canvas.o picker.o utils.o history.o prompt.o
	$(CC) $(LDFLAGS) -o apint apint.o canvas.o picker.o utils.o \
		history.o prompt.o $(LDLIBS)

clean:
	rm -f apint *.o apint-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f apint $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/apint
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f apint.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/apint.1

dist: clean
	mkdir -p apint-$(VERSION)
	cp -R COPYING config.mk Makefile README apint.1 apint.c \
		canvas.c picker.c utils.c history.c prompt.c canvas.h \
		picker.h utils.h history.h prompt.h apint-$(VERSION)
	tar -cf apint-$(VERSION).tar apint-$(VERSION)
	gzip apint-$(VERSION).tar
	rm -rf apint-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/apint
	rm -f $(DESTDIR)$(MANPREFIX)/man1/apint.1
