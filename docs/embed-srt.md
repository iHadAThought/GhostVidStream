# Embed guide — libghost_srt

**Status:** First-class GhostVidStream decoder plugin (same product bar as `libghost_ndihx`).  
**Header:** `ghost_srt.h` · **media_core id:** `srt` · **Version:** 0.1.x

On AIDA cameras, **SRT sits under RTSP** in the web UI. Treat SRT as a sibling first-class receive path — not a thin side wrapper.

## Dependencies

- FFmpeg shared libs built with **`--enable-libsrt`**
- Runtime **libsrt** (OpenSSL or GnuTLS flavour)
- No SDL, no NDI SDK

## Native embed (C)

```c
#include "ghost_srt.h"

ghost_srt_init();
ghost_srt_options_t opt;
ghost_srt_options_defaults(&opt);
/* Prefer full URL, or set ip + port + stream_id (0=main, 1=sub on AIDA). */
snprintf(opt.url, sizeof(opt.url),
         "srt://172.16.1.189:1600?mode=caller&latency=120&streamid=r=0");
ghost_srt_session_t *s = ghost_srt_session_create(&opt);
ghost_srt_connect_auto(s, NULL);

ghost_srt_frame_t fr;
while (running) {
  if (ghost_srt_capture_newest(s, &fr)) {
    /* fr.data = BGRX, fr.stride = width*4 */
  }
}
ghost_srt_session_destroy(s);
ghost_srt_shutdown();
```

## media_core path

```c
ghost_srt_register_media_module();
const media_module_t *m = media_find_module("srt");
/* open → connect_auto → capture_newest — same as NDI|HX hosts */
```

## GhostVidStream CLI

```bash
ghostvidstream --protocol srt --ip 172.16.1.189 --stats
ghostvidstream --protocol srt --url 'srt://172.16.1.189:1600?mode=caller&latency=120&streamid=r=0'
```

## Caps

- **PTZ:** none over SRT (`MEDIA_CAP_NONE`)
- **Frame format:** BGRX newest-frame drain
- Encode knobs live on the **camera** (shared H.264/H.265 main stream with RTSP/RTMP/NDI|HX)

## Link notes

Link `libghost_srt.a` + `libghost_ffmpeg_rx.a` + `libmedia_core.a` with FFmpeg (`-lavformat -lavcodec -lavutil -lswscale`) and `-lsrt`. Prefer `-Wl,-rpath` to your FFmpeg/libsrt install.

See also: `docs/srt-rtmp-rtsp-encode-compatibility.md`, `docs/modular-compatibility.md`.
