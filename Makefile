# GhostVidStream — portable aarch64 + x86_64
#
# Product / UI: GhostVidStream (SDL viewer + desktop launcher)
# Modules:      libghost_ndihx (NDI|HX), libghost_srt, libghost_rtmp, libghost_rtsp (+ ffmpeg_rx)
# Protocol core: libmedia_core

PREFIX      ?= /usr/local
CC          ?= gcc
PKG_CONFIG  ?= pkg-config
AR          ?= ar

CFLAGS      ?= -O2 -Wall -Wextra -Wno-unused-parameter
CFLAGS      += -Iinclude -I$(PREFIX)/include
LDFLAGS     ?=
LDFLAGS     += -L$(PREFIX)/lib -Wl,-rpath,$(PREFIX)/lib

UNAME_M := $(shell uname -m)
UNAME_S := $(shell uname -s)

# Linux: prefer distro libav (often built with --enable-libsrt) ahead of a
# PREFIX=/usr/local FFmpeg that may lack the SRT protocol ("Protocol not found").
ifeq ($(UNAME_S),Linux)
  MULTIARCH_LIB := $(shell $(CC) -print-multiarch 2>/dev/null)
  ifneq ($(MULTIARCH_LIB),)
    ifneq ($(wildcard /usr/lib/$(MULTIARCH_LIB)/libavformat.so),)
      LDFLAGS := -L/usr/lib/$(MULTIARCH_LIB) -Wl,-rpath,/usr/lib/$(MULTIARCH_LIB) $(LDFLAGS)
    endif
  endif
endif

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 2>/dev/null)
SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 2>/dev/null)
ifeq ($(SDL_LIBS),)
  SDL_LIBS := -lSDL2
endif

FFMPEG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libavformat libavcodec libavutil libswscale 2>/dev/null)
FFMPEG_LIBS   := $(shell $(PKG_CONFIG) --libs libavformat libavcodec libavutil libswscale 2>/dev/null)

# libsrt is required for SRT demux at runtime (FFmpeg built with --enable-libsrt)
SRT_LIBS := $(shell $(PKG_CONFIG) --libs srt 2>/dev/null)

NDI_LIBS := -lndi -ldl -lpthread -lm
ifeq ($(UNAME_S),Darwin)
  # Homebrew keg-only ffmpeg-full / srt
  ifneq ($(wildcard /opt/homebrew/opt/ffmpeg-full/lib/pkgconfig),)
    export PKG_CONFIG_PATH := /opt/homebrew/opt/ffmpeg-full/lib/pkgconfig:/opt/homebrew/opt/srt/lib/pkgconfig:/opt/homebrew/lib/pkgconfig:$(PKG_CONFIG_PATH)
    FFMPEG_CFLAGS := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" $(PKG_CONFIG) --cflags libavformat libavcodec libavutil libswscale 2>/dev/null)
    FFMPEG_LIBS   := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" $(PKG_CONFIG) --libs libavformat libavcodec libavutil libswscale 2>/dev/null)
    SRT_LIBS := $(shell PKG_CONFIG_PATH="$(PKG_CONFIG_PATH)" $(PKG_CONFIG) --libs srt 2>/dev/null)
  endif
  ifneq ($(wildcard /opt/homebrew/opt/srt/lib),)
    LDFLAGS += -L/opt/homebrew/opt/srt/lib -Wl,-rpath,/opt/homebrew/opt/srt/lib
  endif
  ifneq ($(wildcard /opt/homebrew/opt/ffmpeg-full/lib),)
    LDFLAGS += -L/opt/homebrew/opt/ffmpeg-full/lib -Wl,-rpath,/opt/homebrew/opt/ffmpeg-full/lib
    CFLAGS  += -I/opt/homebrew/opt/ffmpeg-full/include
  endif
endif

ifeq ($(FFMPEG_LIBS),)
  FFMPEG_LIBS := -lavformat -lavcodec -lavutil -lswscale
endif
ifeq ($(SRT_LIBS),)
  SRT_LIBS := -lsrt
endif

URL_LIBS := $(FFMPEG_LIBS) $(SRT_LIBS) -lpthread -lm

DESKTOP_DIR := $(DESTDIR)$(PREFIX)/share/applications
ICON_DIR    := $(DESTDIR)$(PREFIX)/share/icons/hicolor/512x512/apps

.PHONY: all clean install install-lib install-viewer install-desktop info stress stress-asan stress-url stress-url-asan

all: info libmedia_core.a libghost_ndihx.a libghost_ffmpeg_rx.a libghost_srt.a libghost_rtmp.a libghost_rtsp.a ghostvidstream

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

libghost_ffmpeg_rx.a: src/modules/ffmpeg_rx/ffmpeg_rx.c include/ffmpeg_rx.h
	$(CC) $(CFLAGS) $(FFMPEG_CFLAGS) -c -o ffmpeg_rx.o src/modules/ffmpeg_rx/ffmpeg_rx.c
	$(AR) rcs $@ ffmpeg_rx.o
	rm -f ffmpeg_rx.o

libghost_srt.a: src/modules/srt/ghost_srt.c include/ghost_srt.h include/ffmpeg_rx.h include/media_core.h libghost_ffmpeg_rx.a libmedia_core.a
	$(CC) $(CFLAGS) -c -o ghost_srt.o src/modules/srt/ghost_srt.c
	$(AR) rcs $@ ghost_srt.o
	rm -f ghost_srt.o

libghost_rtmp.a: src/modules/rtmp/ghost_rtmp.c include/ghost_rtmp.h include/ffmpeg_rx.h include/media_core.h libghost_ffmpeg_rx.a libmedia_core.a
	$(CC) $(CFLAGS) -c -o ghost_rtmp.o src/modules/rtmp/ghost_rtmp.c
	$(AR) rcs $@ ghost_rtmp.o
	rm -f ghost_rtmp.o
libghost_rtsp.a: src/modules/rtsp/ghost_rtsp.c include/ghost_rtsp.h include/ffmpeg_rx.h include/media_core.h libghost_ffmpeg_rx.a libmedia_core.a
	$(CC) $(CFLAGS) -c -o ghost_rtsp.o src/modules/rtsp/ghost_rtsp.c
	$(AR) rcs $@ ghost_rtsp.o
	rm -f ghost_rtsp.o


ghostvidstream: src/viewer_main.c libghost_ndihx.a libghost_srt.a libghost_rtmp.a libghost_rtsp.a libghost_ffmpeg_rx.a libmedia_core.a \
		include/ghost_ndihx.h include/ghost_srt.h include/ghost_rtmp.h include/ghost_rtsp.h include/media_core.h
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ src/viewer_main.c \
		libghost_ndihx.a libghost_srt.a libghost_rtmp.a libghost_rtsp.a libghost_ffmpeg_rx.a libmedia_core.a \
		$(LDFLAGS) $(NDI_LIBS) $(URL_LIBS) $(SDL_LIBS)
	ln -sfn ghostvidstream ndi-hx-viewer

ndi-hx-viewer: ghostvidstream

stress: stress_ghost_ndihx

stress_ghost_ndihx: tests/stress/stress_ghost_ndihx.c libghost_ndihx.a libmedia_core.a
	$(CC) $(CFLAGS) -o $@ tests/stress/stress_ghost_ndihx.c \
		libghost_ndihx.a libmedia_core.a \
		$(LDFLAGS) $(NDI_LIBS)

stress-url: stress_url_rx

stress_url_rx: tests/stress/stress_url_rx.c libghost_srt.a libghost_rtmp.a libghost_rtsp.a libghost_ffmpeg_rx.a libmedia_core.a
	$(CC) $(CFLAGS) $(FFMPEG_CFLAGS) -o $@ tests/stress/stress_url_rx.c \
		libghost_srt.a libghost_rtmp.a libghost_rtsp.a libghost_ffmpeg_rx.a libmedia_core.a \
		$(LDFLAGS) $(URL_LIBS)

stress-url-asan: stress_url_rx-asan

stress_url_rx-asan: tests/stress/stress_url_rx.c src/modules/srt/ghost_srt.c src/modules/rtmp/ghost_rtmp.c \
		src/modules/rtsp/ghost_rtsp.c src/modules/ffmpeg_rx/ffmpeg_rx.c src/core/media_core.c
	$(CC) $(CFLAGS) $(FFMPEG_CFLAGS) -O1 -g -fsanitize=address -fno-omit-frame-pointer \
		-o $@ tests/stress/stress_url_rx.c src/modules/srt/ghost_srt.c src/modules/rtmp/ghost_rtmp.c \
		src/modules/rtsp/ghost_rtsp.c src/modules/ffmpeg_rx/ffmpeg_rx.c src/core/media_core.c \
		$(LDFLAGS) $(URL_LIBS)

stress-asan: stress_ghost_ndihx-asan

stress_ghost_ndihx-asan: tests/stress/stress_ghost_ndihx.c src/modules/ghost_ndihx/ghost_ndihx.c src/core/media_core.c
	$(CC) $(CFLAGS) -O1 -g -fsanitize=address -fno-omit-frame-pointer \
		-o $@ tests/stress/stress_ghost_ndihx.c src/modules/ghost_ndihx/ghost_ndihx.c src/core/media_core.c \
		$(LDFLAGS) $(NDI_LIBS)

install: install-lib install-viewer install-desktop

install-lib: libmedia_core.a libghost_ndihx.a libghost_ffmpeg_rx.a libghost_srt.a libghost_rtmp.a libghost_rtsp.a
	install -d $(DESTDIR)$(PREFIX)/include $(DESTDIR)$(PREFIX)/lib
	install -m 644 include/media_core.h $(DESTDIR)$(PREFIX)/include/media_core.h
	install -m 644 include/ghost_ndihx.h $(DESTDIR)$(PREFIX)/include/ghost_ndihx.h
	install -m 644 include/ghost_srt.h $(DESTDIR)$(PREFIX)/include/ghost_srt.h
	install -m 644 include/ghost_rtmp.h $(DESTDIR)$(PREFIX)/include/ghost_rtmp.h
	install -m 644 include/ghost_rtsp.h $(DESTDIR)$(PREFIX)/include/ghost_rtsp.h
	install -m 644 include/ffmpeg_rx.h $(DESTDIR)$(PREFIX)/include/ffmpeg_rx.h
	install -m 644 libmedia_core.a $(DESTDIR)$(PREFIX)/lib/libmedia_core.a
	install -m 644 libghost_ndihx.a $(DESTDIR)$(PREFIX)/lib/libghost_ndihx.a
	install -m 644 libghost_ffmpeg_rx.a $(DESTDIR)$(PREFIX)/lib/libghost_ffmpeg_rx.a
	install -m 644 libghost_srt.a $(DESTDIR)$(PREFIX)/lib/libghost_srt.a
	install -m 644 libghost_rtmp.a $(DESTDIR)$(PREFIX)/lib/libghost_rtmp.a
	install -m 644 libghost_rtsp.a $(DESTDIR)$(PREFIX)/lib/libghost_rtsp.a
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
	rm -f ghostvidstream ndi-hx-viewer \
		libghost_ndihx.a libghost_srt.a libghost_rtmp.a libghost_rtsp.a libghost_ffmpeg_rx.a libmedia_core.a *.o \
		stress_ghost_ndihx stress_ghost_ndihx-asan stress_url_rx stress_url_rx-asan \
		libndi_hx.a stress_ndi_hx stress_ndi_hx-asan
