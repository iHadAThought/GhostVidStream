# GhostVidStream — portable aarch64 + x86_64
#
# Product / UI: GhostVidStream (SDL viewer + desktop launcher)
# Library:      libghost_ndihx  (header ghost_ndihx.h, API ghost_ndihx_*)
# Protocol core: libmedia_core
#
# Layout (modular-ready):
#   include/media_core.h
#   include/ghost_ndihx.h
#   src/core/media_core.c
#   src/modules/ghost_ndihx/   NDI|HX module (first)
#   src/modules/{ndi_full,st2110,rtsp}/  placeholders
#   src/viewer_main.c          GhostVidStream SDL app
#   assets/ghostvidstream.png
#   packaging/ghostvidstream.desktop
#
# Builds: libmedia_core.a + libghost_ndihx.a + ghostvidstream
#          (+ ndi-hx-viewer → ghostvidstream symlink for old habits)

PREFIX      ?= /usr/local
CC          ?= gcc
PKG_CONFIG  ?= pkg-config
AR          ?= ar

CFLAGS      ?= -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS      += -Iinclude -I$(PREFIX)/include
LDFLAGS     ?=
LDFLAGS     += -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib

UNAME_M := $(shell uname -m)

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
ifeq ($(SDL_LIBS),)
  SDL_LIBS := -lSDL2
endif

NDI_LIBS := -lndi -ldl -lpthread -lm

DESKTOP_DIR := $(DESTDIR)$(PREFIX)/share/applications
ICON_DIR    := $(DESTDIR)$(PREFIX)/share/icons/hicolor/512x512/apps

.PHONY: all clean install install-lib install-viewer install-desktop info stress stress-asan

all: info libmedia_core.a libghost_ndihx.a ghostvidstream

info:
	@echo "Building GhostVidStream for arch=$(UNAME_M) PREFIX=$(PREFIX)"

libmedia_core.a: src/core/media_core.c include/media_core.h
	$(CC) $(CFLAGS) -c -o media_core.o src/core/media_core.c
	$(AR) rcs $@ media_core.o
	rm -f media_core.o

libghost_ndihx.a: src/modules/ghost_ndihx/ghost_ndihx.c include/ghost_ndihx.h include/media_core.h libmedia_core.a
	$(CC) $(CFLAGS) -c -o ghost_ndihx.o src/modules/ghost_ndihx/ghost_ndihx.c
	$(AR) rcs $@ ghost_ndihx.o
	rm -f ghost_ndihx.o

ghostvidstream: src/viewer_main.c libghost_ndihx.a libmedia_core.a include/ghost_ndihx.h include/media_core.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ src/viewer_main.c \
		libghost_ndihx.a libmedia_core.a \
		$(LDFLAGS) $(NDI_LIBS) $(SDL_LIBS)
	ln -sfn ghostvidstream ndi-hx-viewer

# Legacy alias target
ndi-hx-viewer: ghostvidstream

stress: stress_ghost_ndihx

stress_ghost_ndihx: tests/stress/stress_ghost_ndihx.c libghost_ndihx.a libmedia_core.a
	$(CC) $(CFLAGS) -o $@ tests/stress/stress_ghost_ndihx.c \
		libghost_ndihx.a libmedia_core.a \
		$(LDFLAGS) $(NDI_LIBS)

stress-asan: stress_ghost_ndihx-asan

stress_ghost_ndihx-asan: tests/stress/stress_ghost_ndihx.c src/modules/ghost_ndihx/ghost_ndihx.c src/core/media_core.c
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address -fno-omit-frame-pointer \
		-o $@ tests/stress/stress_ghost_ndihx.c src/modules/ghost_ndihx/ghost_ndihx.c src/core/media_core.c \
		$(LDFLAGS) $(NDI_LIBS)

install: install-lib install-viewer install-desktop

install-lib: libmedia_core.a libghost_ndihx.a
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	install -m 644 include/media_core.h $(DESTDIR)$(PREFIX)/include/media_core.h
	install -m 644 include/ghost_ndihx.h $(DESTDIR)$(PREFIX)/include/ghost_ndihx.h
	install -m 644 libmedia_core.a $(DESTDIR)$(PREFIX)/lib/libmedia_core.a
	install -m 644 libghost_ndihx.a $(DESTDIR)$(PREFIX)/lib/libghost_ndihx.a
	# Remove superseded library/header names if present from older installs
	rm -f $(DESTDIR)$(PREFIX)/include/ndi_hx.h $(DESTDIR)$(PREFIX)/lib/libndi_hx.a

install-viewer: ghostvidstream
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 ghostvidstream $(DESTDIR)$(PREFIX)/bin/ghostvidstream
	ln -sfn ghostvidstream $(DESTDIR)$(PREFIX)/bin/ndi-hx-viewer

install-desktop: assets/ghostvidstream.png packaging/ghostvidstream.desktop
	install -d $(DESKTOP_DIR) $(ICON_DIR)
	install -m 644 packaging/ghostvidstream.desktop $(DESKTOP_DIR)/ghostvidstream.desktop
	install -m 644 packaging/ghostvidstream.desktop $(DESKTOP_DIR)/ndi-hx-viewer.desktop
	install -m 644 assets/ghostvidstream.png $(ICON_DIR)/ghostvidstream.png
	install -m 644 assets/ghostvidstream.png $(ICON_DIR)/ndi-hx-viewer.png
	-gtk-update-icon-cache -f $(DESTDIR)$(PREFIX)/share/icons/hicolor 2>/dev/null || true
	-update-desktop-database $(DESTDIR)$(PREFIX)/share/applications 2>/dev/null || true

clean:
	rm -f ghostvidstream ndi-hx-viewer libghost_ndihx.a libmedia_core.a *.o \
		stress_ghost_ndihx stress_ghost_ndihx-asan \
		libndi_hx.a stress_ndi_hx stress_ndi_hx-asan
