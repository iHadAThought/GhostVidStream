# Module: rtsp

GhostVidStream **RTSP/RTP** decoder plugin (`ghost_rtsp`).

Implements `media_module_t` (`id=rtsp`, `MEDIA_PROTO_RTSP`) via shared `ffmpeg_rx`
BGRX path. Native API: `include/ghost_rtsp.h`.

Default AIDA path: `rtsp://IP:554/stream/main`.
