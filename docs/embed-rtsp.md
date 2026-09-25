# Embed guide — libghost_rtsp

**Status:** First-class GhostVidStream decoder plugin (same product bar as `libghost_ndihx` / `libghost_srt`).  
**Header:** `ghost_rtsp.h` · **media_core id:** `rtsp` · **Version:** 0.1.x

On AIDA, **RTSP** and **SRT** are adjacent UI siblings — both ship as equal first-class modules.

## Dependencies

- FFmpeg (RTSP/RTP demux + H.264/H.265 decode)
- No SDL, no NDI SDK

## Native embed (C)

```c
#include "ghost_rtsp.h"

ghost_rtsp_init();
ghost_rtsp_options_t opt;
ghost_rtsp_options_defaults(&opt);
snprintf(opt.ip_substr, sizeof(opt.ip_substr), "%s", "172.16.1.189");
/* Default: rtsp://IP:554/stream/main — TCP interleaved on */
ghost_rtsp_session_t *s = ghost_rtsp_session_create(&opt);
ghost_rtsp_connect_auto(s, NULL);

ghost_rtsp_frame_t fr;
while (running) {
  if (ghost_rtsp_capture_newest(s, &fr)) {
    /* BGRX */
  }
}
ghost_rtsp_session_destroy(s);
ghost_rtsp_shutdown();
```

## GhostVidStream CLI

```bash
ghostvidstream --protocol rtsp --ip 172.16.1.189 --stats
ghostvidstream --protocol rtsp --url rtsp://172.16.1.189:554/stream/main
```

## Caps

- **PTZ:** none in this module (VISCA / camera CGI remains separate)
- Prefer TCP transport for show LANs that drop UDP RTSP

See `docs/embed-srt.md` and `docs/srt-rtmp-rtsp-encode-compatibility.md`.
