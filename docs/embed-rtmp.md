# Embed guide — libghost_rtmp

**Status:** First-class GhostVidStream decoder plugin (same product bar as `libghost_ndihx` / `libghost_srt`).  
**Header:** `ghost_rtmp.h` · **media_core id:** `rtmp` · **Version:** 0.1.x

## Dependencies

- FFmpeg with RTMP demux (Ubuntu apt `ffmpeg` / Homebrew `ffmpeg-full`)
- No SDL, no NDI SDK

## Native embed (C)

```c
#include "ghost_rtmp.h"

ghost_rtmp_init();
ghost_rtmp_options_t opt;
ghost_rtmp_options_defaults(&opt);
snprintf(opt.ip_substr, sizeof(opt.ip_substr), "%s", "172.16.1.189");
/* Default builds rtmp://IP:1935/app/rtmpstream0 */
ghost_rtmp_session_t *s = ghost_rtmp_session_create(&opt);
ghost_rtmp_connect_auto(s, NULL);

ghost_rtmp_frame_t fr;
while (running) {
  if (ghost_rtmp_capture_newest(s, &fr)) {
    /* BGRX */
  }
}
ghost_rtmp_session_destroy(s);
ghost_rtmp_shutdown();
```

HTTP-FLV alternate: set `opt.prefer_flv = true` or pass a full `http://…/app/rtmpstream0.flv` URL.

## GhostVidStream CLI

```bash
ghostvidstream --protocol rtmp --ip 172.16.1.189 --stats
ghostvidstream --protocol rtmp --url rtmp://172.16.1.189:1935/app/rtmpstream0
```

## Caps

- **PTZ:** none (`MEDIA_CAP_NONE`)
- Camera **RTMP push** UI is a *destination*; this module **pulls** from the camera’s local SRS.

See `docs/embed-srt.md` for the shared lifecycle pattern and `docs/srt-rtmp-rtsp-encode-compatibility.md` for encode matrix results.
