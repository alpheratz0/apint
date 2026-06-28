# apint

![apint](misc/screenshot-1.png)

## Description

apint is a primitive paint application for X.

For usage instructions, check:

```sh
$ man apint
```

## Dependencies

To build apint, you need the following libraries installed: libxcb, libxcb-cursor, libxcb-image, libxcb-shm, libxcb-keysyms, libxcb-xkb, libxcb-icccm and libpng, plus dmenu/rofi and notify-send at runtime.

## Building and installing

```sh
$ make
$ sudo make PREFIX=/usr install
```

## License

Code is licensed under GNU's General Public License v2. See `COPYING` for details.
