# Resource usage (GhostVidStream viewer)

Steady-state measurements for the **GhostVidStream** SDL viewer on Linux
**aarch64**, with the active decoder plugin **libghost_ndihx** (NDI|HX).

Future protocol modules (FULL NDI / 2110 / RTSP) will publish their own tables;
numbers below are **NDI|HX only**.

## Test setup

| Item | Value |
| --- | --- |
| Host | `172.16.1.144` · Ubuntu · **4× aarch64** cores |
| Camera | `172.16.1.189` (HX stream, typically 1920×1080 @ 30) |
| When | 2026-09-25 (baseline + harden remeasure) |
| Binary | `ghostvidstream` / legacy `ndi-hx-viewer` |
| Method | ~12 s warmup, then `pidstat` / `/proc` jiffies over ~5 s; RSS/threads/fds from `/proc` |
| Window | Windowed (not fullscreen) for profiling |

**CPU %** is on a **one-core = 100%** scale (Linux `pidstat` / proc accounting). On a
4-core box, ~70% ≈ under one full core busy — not “70% of the machine.”

No credentials or secrets are required to interpret these numbers.

## Steady-state (viewer)

| Scenario | CPU % | RSS | Threads | FDs | Resolution | Src fps |
| --- | ---: | ---: | ---: | ---: | --- | ---: |
| **No stream** (auto-search, unmatched IP) | **~0** | **~9.6 MiB** | **2** | **6** | — | — |
| **Low quality** (`bandwidth=lowest`) | **~72–78** | **~190–194 MiB** | **29** | **38** | **640×360** | 30 |
| **Max quality** (`bandwidth=highest`) | **~65–80** | **~217–227 MiB** | **29** | **38** | **1920×1080** | 30 |
| Stats HUD on (`--stats` / `i`) | ~67–83 | ~217–231 MiB | 29 | 38 | 1920×1080 | 30 |
| Controls overlay on (`c`) | ~69 | ~217 MiB | 29 | 38 | 1920×1080 | 30 |
| Paused (Space), **after harden** | ~78 | **~227 MiB** | 29 | 38 | 1920×1080 | 30 |

Ranges span the baseline profile and the post-harden remeasure (same host/camera).
Sample variance on aarch64 software HX decode is high; treat CPU as ±10–15 points.

## Takeaways (app / booth)

1. **Idle is cheap** — finder-only, ~0% CPU, ~10 MiB RSS.
2. **Decode dominates** — connecting jumps to ~29 threads / ~190–230 MiB RSS (libndi + FFmpeg HX workers).
3. **Overlays barely move the needle** — stats HUD and controls add a few % CPU / few MiB vs decode.
4. **Lowest bandwidth** still costs ~70%+ CPU on this box (software HX) while delivering 640×360; highest delivers full 1080p at similar CPU class.
5. **Pause** now drains the receive queue (harden pass) so RSS stays ≈ live (~227 MiB), not a ballooned queue (~296 MiB class before the fix).

## Soak (decoder path under the viewer)

Headless / stress harness on the same host (newest-frame drain, 1080p):

| Metric | Approx. |
| --- | --- |
| Capture drain rate | ~74–132 fps (not glass-to-glass) |
| RSS growth (45–180 s soak) | ~25–50 MiB (under 64 MiB fail threshold) |
| Stress suites | discover / reconnect / bw flap / PTZ / multi / wrong-IP — **PASS** |

Re-run helpers (on a deployed tree): `tests/stress/run_all.sh`,
`tests/stress/resource_profile.sh`.

## Related

- Library-framed copy of these measurements: sibling repo **libghost_ndihx** → `docs/resource-usage.md`
- Embed / API: [integration.md](integration.md)
- Multi-protocol shell: [modular-compatibility.md](modular-compatibility.md)
