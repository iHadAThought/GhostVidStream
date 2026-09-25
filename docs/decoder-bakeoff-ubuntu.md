# Decoder bake-off: GhostVidStream SRT/RTMP vs Ubuntu apt FFmpeg

**Date:** 2026-09-25  
**Host:** `172.16.1.144` (NDIHX) · Ubuntu 26.04.1 LTS · aarch64 · 4 cores · ~7.4 GiB RAM  
**Camera:** AIDA HD-NDI-X20 @ `172.16.1.189` · booth baseline **1080p30 H.264**  
**Stock stack:** Ubuntu apt `ffmpeg` / `ffplay` **8.0.1-3ubuntu2** (`--enable-libsrt`, protocols include `srt` + `rtmp`)  
**Ours:** `stress_url_rx` → `ghost_srt` / `ghost_rtmp` linked against the **same apt libav\*** (not Homebrew)

No passwords in this document.

## URLs exercised

| Protocol | URL |
| --- | --- |
| SRT | `srt://172.16.1.189:1600?mode=caller&latency=120&streamid=r=0` |
| RTMP | `rtmp://172.16.1.189:1935/app/rtmpstream0` |

## Method

- Soak ≈ **35 s** after connect; CPU% = one-core scale (`ps`); RSS = peak `VmRSS`.
- **Ours:** `capture_newest` BGRX drain (`stress_url_rx --soak 35 --reconnect 0`).
- **Ubuntu default:** `ffmpeg -i URL -an -f null -` (stock flags).
- **Ubuntu tuned:** `-fflags nobuffer -flags low_delay -probesize 32768 -analyzeduration 500000` (+ `-rtmp_live live` for RTMP).
- Aggressive probes (`probesize 32`, `-avioflags direct`) **failed** open on live SRT/RTMP — noted below, not used in the table.
- TTFF = wall ms from process start → first decoded/presented frame.
- `ffplay -nodisp` failed filtergraph on this session; **mpv/vlc not installed** — skipped.

## Results

| Protocol | Decoder | Mode | TTFF (ms) | Steady fps\* | Avg CPU% | Peak RSS (MiB) | Notes |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- |
| SRT | **ghost (ours)** | BGRX | **1329** | **29.9** | 11.6 | **59** | drops=0 |
| SRT | apt ffmpeg | default | 5815 | 34† | 12.1 | 126 | slow probe |
| SRT | apt ffmpeg | tuned | 1831 | 29 | 14.2 | 79 | ~1.0× realtime |
| RTMP | **ghost (ours)** | BGRX | 1782 | **14.1**‡ | 9.2 | **56** | drops=0 |
| RTMP | apt ffmpeg | default | 2507 | 31† | 12.1 | 111 | null sink |
| RTMP | apt ffmpeg | tuned | **1760** | 29 | 9.2 | 75 | ~1.0× realtime |

\*fps definitions differ — see caveats.  
†ffmpeg default reported decode throughput into `null` (can exceed 30).  
‡our RTMP present rate under the BGRX poll harness (camera still 1080p30; same behavior as Mac stress pass).

## Takeaways

1. **TTFF:** Ours SRT is fastest here (~1.3 s). Apt default SRT is slow (~5.8 s) until mild low-latency flags bring it to ~1.8 s — near ours. RTMP TTFF is roughly tied (ours ~1.8 s vs tuned apt ~1.8 s).
2. **RSS:** Ours uses ~half the peak RSS of apt default null-decode (~56–59 vs ~111–126 MiB) on this host; tuned apt sits in between (~75–79).
3. **CPU:** Same order of magnitude (~9–14% of one core) for SRT/RTMP decode at 1080p30. Tuned SRT apt used slightly more CPU than ours.
4. **SRT present rate:** Ours holds camera cadence (~30 fps BGRX). Apt tuned tracks realtime (~29).
5. **RTMP present rate:** Apt null-sink shows full 29–31 decode fps; our module’s `capture_newest` path still presents ~14 fps — known harness/path difference, **not** a camera encode shortfall.

## Honest caveats

- **Display path differs:** Ours measures media_core **BGRX newest-frame drain** (embed-shaped). Apt path is **demux → decode → discard** (`-f null`). Neither is glass-to-glass lag with an SDL present.
- **fps metrics are not identical:** Ours counts successful `capture_newest` returns; ffmpeg prints encode/decode pipeline fps into null (and default can run >1.0×).
- **Same libav underneath:** Ours was deliberately linked to Ubuntu apt libav 8.0.1 so this compares **our module/lifecycle** vs **stock CLI**, not a private FFmpeg build vs apt.
- **“Ubuntu built-in”** here means **apt universe ffmpeg 8.0.1** on Ubuntu 26.04. Older LTS images may ship different versions / SRT enablement.
- **ffplay / VLC / mpv** not in the table (ffplay `-nodisp` failed; players not installed).
- Glass-to-glass / subjective lag not instrumented (no genlock / LED probe).

## Restore

- Camera left at booth venc baseline; SRT listen remained enabled from prior pass.
- Host `ndi-hx-viewer` binary untouched at `/usr/local/bin/ndi-hx-viewer`; viewer was idle before/after (no booth UI session restarted).
- Apt `ffmpeg` + `-dev` packages were **installed** for the bake-off and left installed (useful for ops).

## Related

- [srt-rtmp-decoder.md](srt-rtmp-decoder.md) — endpoints, module layout, earlier Mac stress PASS
- In-repo: `docs/decoder-bakeoff-ubuntu.md` (GhostVidStream)
