# AIDA encode compatibility — SRT / RTMP / RTSP (GhostVidStream)

**Date:** 2026-09-25  
**Camera:** AIDA HD-NDI-X20 @ `172.16.1.189`  
**Decoders:** first-class `ghost_srt` / `ghost_rtmp` / `ghost_rtsp` via `stress_url_rx` (same `media_core` + `ffmpeg_rx` path as embeds)  
**Host:** Mac LAN lab (same show LAN as camera). Decoder box `172.16.1.144` used for prior NDI|HX matrix; this pass exercised the URL modules directly.

No passwords in this document. Camera CGI credentials were used only for live `venc` set/get.

## Verdict

| Protocol | Matrix (connect + first frame) | Stress subset | Notes |
| --- | --- | --- | --- |
| **SRT** (`libghost_srt`) | **29/29 PASS** (after H.265 re-probe) | **4/4 PASS** | First-class; matches NDI\|HX product bar |
| **RTSP** (`libghost_rtsp`) | **29/29 PASS** | **4/4 PASS** | AIDA UI sibling to SRT |
| **RTMP** (`libghost_rtmp`) | **26/29 PASS** | **3/4 PASS** | H.264 solid; **H.265 pull intermittent / hang** |

Shared H.264/H.265 main-stream knobs that worked for NDI|HX also work for **SRT** and **RTSP**. RTMP is reliable on **H.264**; several **H.265** cells timed out or failed to connect (camera encoder restart + FFmpeg RTMP demux). Not an NDI-only issue — NDI bandwidth knobs remain NDI-only and were not applied here.

## Camera knobs (same as NDI|HX matrix)

| Knob | Values swept |
| --- | --- |
| Mode | H.264, H.265 |
| H.264 profile | MP, HP |
| Format | 1080p @ 60/59.94/50/30/29.97/25; 720p @ 60/30/25 |
| Bitrate | 1024–16384 kbps |
| Rate control | CBR, VBR |
| GOP | 3, 15, 30, 60, 120 |
| Intersections | H.265 1080p60, H.265 VBR, H.264 HP 1080p60, min bitrate @ 60fps |

**NDI-only (skipped):** viewer `--bandwidth` low/high — does not apply to SRT/RTMP/RTSP.

## How we tested

1. Camera CGI login → set main `venc` cell → wait for encoder restart → probe each protocol with `stress_url_rx --soak 3 --reconnect 0`.
2. Require `PASS first_frame` at expected WxH and `RESULT PASS`.
3. Stress subset: baseline 1080p30, 1080p60, H.265 1080p30, 720p60 — soak 20s + 2 reconnects.
4. Re-probe early H.265 failures with longer settle (SRT H.265 **PASS** on re-probe).
5. Restore booth baseline: H.264 MP 1080p30 CBR 4096 GOP 30 (+ 720p30 sub).

## Failures / caveats

| Cell | SRT | RTSP | RTMP |
| --- | --- | --- | --- |
| mode H.265 1080p30 | PASS (re-probe) | PASS | FAIL / timeout |
| H.265 1080p60 | PASS | PASS | FAIL / timeout |
| H.265 VBR 1080p30 | PASS | PASS | FAIL / timeout |
| stress H.265 | PASS | PASS | FAIL |

Treat **RTMP + H.265** as unsupported for show use until a dedicated demux settle path lands; prefer **SRT or RTSP** (or H.264 RTMP).

## Stress summary

| Setting | SRT | RTSP | RTMP |
| --- | --- | --- | --- |
| H.264 baseline 1080p30 | PASS | PASS | PASS |
| 1080p60 | PASS | PASS | PASS |
| H.265 1080p30 | PASS | PASS | FAIL |
| 720p60 | PASS | PASS | PASS |

## Viewer / embed

```bash
ghostvidstream --protocol srt  --ip 172.16.1.189 --stats
ghostvidstream --protocol rtsp --ip 172.16.1.189 --stats
ghostvidstream --protocol rtmp --ip 172.16.1.189 --stats
```

Embed guides: `docs/embed-srt.md`, `docs/embed-rtmp.md`, `docs/embed-rtsp.md`.

## Restore

Camera main/sub restored to booth baseline after the sweep. SRT listen left enabled (port 1600) for continued module use.
