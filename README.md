# GhostVidStream

**GhostVidStream** is the product name for this low-latency **NDI|HX** receive stack on
**Linux aarch64 and x86_64**.

| Piece | What it is |
| --- | --- |
| **GhostVidStream** | Product / SDL reference viewer + desktop launcher |
| `libghost_ndihx` (`ghost_ndihx.h`, `ghost_ndihx_*`) | Embeddable C library — NDI\|HX focused, no GUI |
| `libmedia_core` | Protocol-agnostic module registry + PTZ capability contract |
| Desktop launcher | **GhostVidStream** → `ghostvidstream --auto` |
| `install-deps.sh` | Installs libndi (SDK v6) + FFmpeg ≥ 7 + SDL2 build deps |

Technical names (stable for embeds): **`libghost_ndihx`** / **`ghost_ndihx.h`** /
**`ghost_ndihx_*`**. The viewer binary is **`ghostvidstream`** (symlink
`ndi-hx-viewer` kept for old habits).

Camera encode (resolution / fps / Hz on the wire) is controlled by the **sender**.
This project optimizes the **receiver**: newest-frame drain, optional low bandwidth,
auto LAN search, and display-side caps in GhostVidStream.

Validated against `HD-NDI-X20 (HX-Stream-172.16.1.189)` on Ubuntu aarch64.

## Quick start (Linux)

```bash
./install-deps.sh          # needs sudo; /usr/local
make                       # libmedia_core.a + libghost_ndihx.a + ghostvidstream
sudo make install          # headers, .a, binary, icon, .desktop → /usr/local

./ghostvidstream --list
./ghostvidstream --ip 172.16.1.189 --stats
./ghostvidstream --config config.example.conf --fullscreen
# Or launch from GNOME: “GhostVidStream” (Exec=ghostvidstream --auto)
# Legacy: ndi-hx-viewer → same binary
```

Prefer a wired NDI/General NIC. VMs without a GPU decode in software (higher CPU).

## Viewer settings

| Flag / config key | Effect |
| --- | --- |
| `--auto` / `auto_search` | Keep scanning until a camera matches; reconnect on silence |
| `--ip` / `ip` | Prefer source whose name/url contains this host |
| `--source` / `source` | Prefer name/url substring |
| `--bandwidth` / `bandwidth` | `highest` or `lowest` (lighter decode) |
| `--max-w` `--max-h` | Cap letterboxed display size |
| `--fps-cap` / `fps_cap` | Cap present rate (CPU) |
| `--hz` / `hz` | Best-effort display refresh hint |
| `--stats` | Start with on-screen stats HUD (also toggle with `i`) |

### On-screen UI (SDL only)

| Key | Action |
| --- | --- |
| `c` | Toggle light **controls** overlay (video keeps playing; no dim) |
| `i` | Toggle **stats** HUD (resolution, src/present fps, bandwidth, source) |
| `q` / Esc | Quit |
| `f` | Fullscreen |
| Space | Pause |
| `r` | Rescan / reconnect |
| `[` / `]` | Bandwidth lowest / highest |
| `-` / `=` | FPS cap step · `0` uncapped |

Controls overlay also exposes auto-search, max W/H, rescan, stats checkbox, and a
**PTZ pad** only when the active module reports `MEDIA_CAP_PTZ` (NDI|HX probes
`NDIlib_recv_ptz_is_supported` after connect). Pure receive sources show no PTZ chrome.

## Latency & leaks

- Capture path always **drains to the newest frame** and frees intermediates
  (also continues past NDI `status_change` so PTZ/capability events cannot strand
  stale frames). `ghost_ndihx_drain()` discards the queue when the host is paused.
- Audio/metadata are not pulled (lower overhead for video-only monitors).
- Default bandwidth is **highest** (full sender resolution, BGRX). `lowest` is an
  explicit opt-in — never a silent quality drop.
- Vsync off in GhostVidStream; optional fps-cap when you want less CPU.
- Tear down with `ghost_ndihx_session_destroy` (frees last frame + receiver + finder).

## Embed in other apps

See **[docs/integration.md](docs/integration.md)** for the full implement-in-another-app
guide (API map, link flags, threading, PTZ, aarch64/x86_64). Modular hosts:
**[docs/modular-compatibility.md](docs/modular-compatibility.md)**.

```c
#include <ghost_ndihx.h>
/* ghost_ndihx_session_create → ghost_ndihx_connect_auto → ghost_ndihx_capture_newest */
/* Pause path: ghost_ndihx_drain(session) so the receive queue cannot grow */
/* Optional: ghost_ndihx_capabilities / ghost_ndihx_ptz_* when MEDIA_CAP_PTZ */
```

Link: `-lghost_ndihx -lmedia_core -lndi -ldl -lpthread -lm` (plus rpath to `PREFIX/lib`).

## Architectures

`install-deps.sh` and the Makefile are arch-portable:

- **aarch64 / arm64** → NDI `lib/aarch64-rpi4-linux-gnueabi`
- **x86_64 / amd64** → NDI `lib/x86_64-linux-gnu`

No arch-hardcoded paths in the build.

## Layout

```
include/media_core.h             Protocol-agnostic module API + PTZ caps
include/ghost_ndihx.h            NDI|HX native API (libghost_ndihx)
src/core/media_core.c            Module registry (no protocol SDKs)
src/modules/ghost_ndihx/         NDI|HX implementation + PTZ probe
src/modules/{ndi_full,st2110,rtsp}/  Placeholders for later protocols
src/viewer_main.c                GhostVidStream SDL app (overlays / HUD)
assets/ghostvidstream.png        Desktop icon (512px)
packaging/ghostvidstream.desktop
docs/integration.md              Native embed guide
docs/modular-compatibility.md    Pluggable main-app contract
config.example.conf              Sample settings
install-deps.sh                  System deps (aarch64 + x86_64)
Makefile
```

## Local only

This project is local-only on Brendan’s machines. Do not push to GitHub.
