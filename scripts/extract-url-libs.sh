#!/usr/bin/env bash
# Extract first-class libghost_{srt,rtmp,rtsp} trees for dual publish.
# Usage: ./scripts/extract-url-libs.sh /tmp/gvs-url-libs
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-/tmp/gvs-url-libs}"
mkdir -p "$DEST"

extract_one() {
  local name="$1" # srt|rtmp|rtsp
  local out="$DEST/libghost_${name}"
  rm -rf "$out"
  mkdir -p "$out"/{include,src/modules/"$name",src/modules/ffmpeg_rx,src/core,docs,tests/stress}
  cp "$ROOT/include/media_core.h" "$out/include/"
  cp "$ROOT/include/ffmpeg_rx.h" "$out/include/"
  cp "$ROOT/include/ghost_${name}.h" "$out/include/"
  cp "$ROOT/src/core/media_core.c" "$out/src/core/"
  cp "$ROOT/src/modules/ffmpeg_rx/ffmpeg_rx.c" "$out/src/modules/ffmpeg_rx/"
  cp "$ROOT/src/modules/${name}/ghost_${name}.c" "$out/src/modules/${name}/"
  cp "$ROOT/docs/embed-${name}.md" "$out/docs/embed.md"
  cp "$ROOT/docs/modular-compatibility.md" "$out/docs/" 2>/dev/null || true
  # Minimal Makefile
  cat > "$out/Makefile" << MAKE
PREFIX ?= /usr/local
CC ?= gcc
PKG_CONFIG ?= pkg-config
AR ?= ar
CFLAGS ?= -O2 -Wall -Wextra -Wno-unused-parameter -Iinclude -I\$(PREFIX)/include
LDFLAGS ?= -L\$(PREFIX)/lib -Wl,-rpath,\$(PREFIX)/lib
FFMPEG_CFLAGS := \$(shell \$(PKG_CONFIG) --cflags libavformat libavcodec libavutil libswscale 2>/dev/null)
FFMPEG_LIBS := \$(shell \$(PKG_CONFIG) --libs libavformat libavcodec libavutil libswscale 2>/dev/null)
ifeq (\$(FFMPEG_LIBS),)
  FFMPEG_LIBS := -lavformat -lavcodec -lavutil -lswscale
endif
SRT_LIBS := \$(shell \$(PKG_CONFIG) --libs srt 2>/dev/null)
ifeq (\$(SRT_LIBS),)
  SRT_LIBS := -lsrt
endif
EXTRA_LIBS := \$(FFMPEG_LIBS) -lpthread -lm
ifeq (${name},srt)
  EXTRA_LIBS += \$(SRT_LIBS)
endif

.PHONY: all clean install
all: libmedia_core.a libghost_ffmpeg_rx.a libghost_${name}.a

libmedia_core.a: src/core/media_core.c include/media_core.h
	\$(CC) \$(CFLAGS) -c -o media_core.o src/core/media_core.c && \$(AR) rcs \$@ media_core.o && rm -f media_core.o

libghost_ffmpeg_rx.a: src/modules/ffmpeg_rx/ffmpeg_rx.c include/ffmpeg_rx.h
	\$(CC) \$(CFLAGS) \$(FFMPEG_CFLAGS) -c -o ffmpeg_rx.o src/modules/ffmpeg_rx/ffmpeg_rx.c && \$(AR) rcs \$@ ffmpeg_rx.o && rm -f ffmpeg_rx.o

libghost_${name}.a: src/modules/${name}/ghost_${name}.c include/ghost_${name}.h libghost_ffmpeg_rx.a libmedia_core.a
	\$(CC) \$(CFLAGS) -c -o ghost_${name}.o src/modules/${name}/ghost_${name}.c && \$(AR) rcs \$@ ghost_${name}.o && rm -f ghost_${name}.o

install: all
	install -d \$(DESTDIR)\$(PREFIX)/include \$(DESTDIR)\$(PREFIX)/lib
	install -m 644 include/*.h \$(DESTDIR)\$(PREFIX)/include/
	install -m 644 libmedia_core.a libghost_ffmpeg_rx.a libghost_${name}.a \$(DESTDIR)\$(PREFIX)/lib/

clean:
	rm -f *.a *.o
MAKE
  cat > "$out/README.md" << READ
# libghost_${name}

First-class **${name^^}** receive library for [GhostVidStream](https://github.com/iHadAThought/GhostVidStream).

Same product bar as \`libghost_ndihx\`: native \`ghost_${name}.h\` API, \`media_core\` plugin id \`${name}\`, BGRX \`capture_newest\`, embed docs.

See \`docs/embed.md\`.

## Build

\`\`\`bash
./Makefile  # or: make
sudo make install
\`\`\`

Requires FFmpeg shared libs$([ "$name" = srt ] && echo " with libsrt" || true).
READ
  # fix bash ${name^^} for mac — rewrite uppercase simply
  sed -i.bak "s/\${name^^}/$(echo "$name" | tr 'a-z' 'A-Z')/g" "$out/README.md" 2>/dev/null || \
    python3 -c "from pathlib import Path; p=Path('$out/README.md'); t=p.read_text().replace('\${name^^}','$(echo $name | tr a-z A-Z)'); p.write_text(t)"
  rm -f "$out/README.md.bak"
  echo "extracted $out"
}

for n in srt rtmp rtsp; do extract_one "$n"; done
ls -la "$DEST"
