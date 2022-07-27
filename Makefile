VERSION = 0.1.0

CC      = cc
CFLAGS  = -std=c99 -pedantic -Wall -Wextra -Os -DVERSION=\"${VERSION}\"
LDLIBS  = -lxcb -lxcb-shm -lm
LDFLAGS = -s ${LDLIBS}

PREFIX    = /usr/local
MANPREFIX = ${PREFIX}/share/man

all: apint

.c.o:
	${CC} -c ${CFLAGS} $<

apint: apint.o
	${CC} -o $@ $< ${LDFLAGS}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f apint ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/apint
	mkdir -p ${DESTDIR}${MANPREFIX}/man1
	cp -f apint.1 ${DESTDIR}${MANPREFIX}/man1
	chmod 644 ${DESTDIR}${MANPREFIX}/man1/apint.1

dist: clean
	mkdir -p apint-${VERSION}
	cp -R LICENSE Makefile README apint.1 apint.c apint-${VERSION}
	tar -cf apint-${VERSION}.tar apint-${VERSION}
	gzip apint-${VERSION}.tar
	rm -rf apint-${VERSION}

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/apint
	rm -f ${DESTDIR}${MANPREFIX}/man1/apint.1

clean:
	rm -f apint apint.o apint-${VERSION}.tar.gz

.PHONY: all clean install uninstall dist
