# SRT + RTMP decoders (GhostVidStream)

**Date:** 2026-09-25  
**Camera:** AIDA HD-NDI-X20 @ `172.16.1.189`  
**Test host:** Mac on same LAN (decoder box `172.16.1.144` unreachable this pass)  
**Checkout:** `/Volumes/My Shared Files/projects/ndi-for-linux`

No passwords in this document. Camera UI credentials were used only for live CGI setup and are not recorded here.

## Camera endpoints (booth baseline encode)

Main stream held at known-good booth settings during tests: **H.264 MP 1920×1080P@30Hz CBR 4096 kbps GOP 30**.

| Protocol | Role on camera | URL used |
| --- | --- | --- |
| **SRT** | Listen mode enabled (port **1600**, latency 120, no encryption) | `srt://172.16.1.189:1600?mode=caller&latency=120&streamid=r=0` (main); `…streamid=r=1` (sub) |
| **RTMP** | Built-in SRS pull server (TCP **1935**) | `rtmp://172.16.1.189:1935/app/rtmpstream0` |
| HTTP-FLV | Same encoder over HTTP **8080** | `http://172.16.1.189:8080/app/rtmpstream0.flv` |
| RTSP | Also present (not a new module this pass) | `rtsp://172.16.1.189:554/stream/main` |

Notes:

- Camera **RTMP “push”** UI (`rtmp.main.enable`) was **off** / empty — that is a push *destination*. Pull uses the camera’s local SRS URLs from `venc.main.rtmpUrl`.
- SRT listen was **disabled** before this work; we enabled listen for testing. Left **enabled** so booth can keep validating; disable via camera UI (`SRT` → listen enable=0) if you want the prior state.
- NDI|HX path unchanged (`HX-Stream-172.16.1.189`).

## Implementation

| Piece | Path |
| --- | --- |
| Shared FFmpeg → BGRX helper | `include/ffmpeg_rx.h`, `src/modules/ffmpeg_rx/ffmpeg_rx.c` |
| SRT module | `include/ghost_srt.h`, `src/modules/srt/ghost_srt.c` (`media` id `srt`) |
| RTMP module | `include/ghost_rtmp.h`, `src/modules/rtmp/ghost_rtmp.c` (`media` id `rtmp`) |
| Protocol ids | `MEDIA_PROTO_SRT=5`, `MEDIA_PROTO_RTMP=6` in `media_core.h` |
| Viewer switch | `--protocol ghost_ndihx\|srt\|rtmp` and `--url` |
| Stress harness | `tests/stress/stress_url_rx.c` → `make stress-url` |

Lifecycle matches `libghost_ndihx`: init → open/options → discover/connect_auto → `capture_newest` (BGRX) → drain/disconnect. **No PTZ** over SRT/RTMP (`MEDIA_CAP_NONE`).

Low-latency FFmpeg opts: `nobuffer`, small `probesize` / `analyzeduration`, `rtmp_live=live`. Do **not** pass RTMP `listen`/`listen_timeout` (that forces server mode).

`install-deps.sh` now pulls `libsrt-*-dev` when available and configures FFmpeg with `--enable-libsrt`.

## Test matrix (`stress_url_rx`)

Host: Apple Silicon Mac · FFmpeg 9 (`ffmpeg-full` + libsrt) · camera as above.

| Suite | SRT | RTMP |
| --- | --- | --- |
| connect + first frame | **PASS** 1920×1080 | **PASS** 1920×1080 |
| soak ~10–20 s | **PASS** ~29.7–29.9 fps present | **PASS** ~14–15 fps present\* |
| reconnect 2–3× | **PASS** | **PASS** |
| disconnect clean | **PASS** | **PASS** |
| HTTP-FLV alt URL | n/a | **PASS** (same module, `--url http://…flv`) |

\*RTMP present rate under the stress harness is lower than SRT on this Mac (still continuous 1080p30 from camera; capture pacing differs). Glass-to-glass not measured.

### Resource / idle notes (order-of-magnitude)

| State | Observation |
| --- | --- |
| Idle / disconnected | Finder-less URL modules: no network session until connect |
| Streaming SRT/RTMP | Decode dominated by H.264 → BGRX (FFmpeg); no NDI ~29-thread stack |
| Overlays | Viewer URL path is minimal (no PTZ chrome) |

Full NDI|HX CPU/RSS table remains in `docs/resource-usage.md`.

## Viewer usage

```bash
./ghostvidstream --protocol srt --ip 172.16.1.189 --stats
./ghostvidstream --protocol rtmp --ip 172.16.1.189 --stats
./ghostvidstream --protocol rtmp --url rtmp://172.16.1.189:1935/app/rtmpstream0
./ghostvidstream --protocol ghost_ndihx --ip 172.16.1.189   # default
```

## Restore status

- Camera **venc** main/sub left at booth baseline (1080p30 H.264 / 720p30 sub).
- **SRT listen left enabled** (port 1600) for continued module testing.
- Decoder host **172.16.1.144** still unreachable — no booth viewer redeploy this pass.
- NDI|HX publish on camera remained enabled throughout.

## BookStack

GhostVidStream + libghost_ndihx Change logs updated for this feature set.

## Bake-off

See [decoder-bakeoff-ubuntu.md](decoder-bakeoff-ubuntu.md) for Ghost modules vs Ubuntu apt ffmpeg on `172.16.1.144`.
