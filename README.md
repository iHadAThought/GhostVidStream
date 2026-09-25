# GhostVidStream

**GhostVidStream** is a multi-protocol **video receive / viewer shell** for
**Linux aarch64 and x86_64**. Protocol decoders plug in behind a shared
`media_core` API; hosts get newest-frame BGRX (or module-documented pixels),
optional PTZ when the active module advertises it, and a common settings surface.

| Piece | What it is |
| --- | --- |
| **GhostVidStream** | Product / SDL reference viewer + desktop launcher |
| `libmedia_core` | Protocol-agnostic module registry + PTZ capability contract |
| `libghost_ndihx` (`ghost_ndihx.h`, `ghost_ndihx_*`) | **First** plug-in decoder — NDI\|HX only (no GUI) |
| `libghost_discover` (`ghost_discover.h`) | Pluggable NDI LAN discovery (Bonjour + NDI SDK) |
| Planned modules | FULL NDI · SMPTE 2110 · RTSP (placeholders under `src/modules/`) |
| Desktop launcher | **GhostVidStream** → `ghostvidstream --auto` |
| `install-deps.sh` | Today: libndi (SDK v6) + FFmpeg ≥ 7 + SDL2 (NDI\|HX module deps) |

The viewer binary is **`ghostvidstream`** (symlink `ndi-hx-viewer` kept for old
habits). Embeds that only need NDI\|HX should link **`libghost_ndihx`** directly
— see that library’s docs / BookStack book. Multi-protocol hosts should prefer
`media_core` + registered modules:
**[docs/modular-compatibility.md](docs/modular-compatibility.md)** (architecture
plan: store `modular-protocol-plan.md`).

**Today’s wired decoder** is NDI\|HX via `libghost_ndihx`. Camera encode
(resolution / fps / Hz on the wire) stays on the **sender**. This project owns
the **receiver shell**: discovery, newest-frame drain, bandwidth where the
module supports it, and display-side caps in the viewer.

Validated NDI\|HX path: `HD-NDI-X20 (HX-Stream-172.16.1.189)` on Ubuntu aarch64.

## Quick start (Linux)

```bash
./install-deps.sh          # needs sudo; /usr/local (NDI|HX module deps today)
make                       # libmedia_core.a + libghost_ndihx.a + ghostvidstream
sudo make install          # headers, .a, binary, icon, .desktop → /usr/local

./ghostvidstream --list
./ghostvidstream --ip 172.16.1.189 --stats
./ghostvidstream --config config.example.conf --fullscreen
# Or launch from GNOME: “GhostVidStream” (Exec=ghostvidstream --auto)
# Legacy: ndi-hx-viewer → same binary
```

Prefer a wired media/General NIC. VMs without a GPU decode in software (higher CPU).

## Viewer settings

| Flag / config key | Effect |
| --- | --- |
| `--auto` / `auto_search` | Keep scanning until a camera matches; reconnect on silence |
| `--discover` / `discover` | `auto` (Bonjour→SDK) · `bonjour` · `ndi_sdk` |
| `--ip` / `ip` | Prefer source whose name/url contains this host |
| `--source` / `source` | Prefer name/url substring |
| `--bandwidth` / `bandwidth` | `highest` or `lowest` (NDI\|HX module) |
| `--max-w` `--max-h` | Cap letterboxed display size |
| `--fps-cap` / `fps_cap` | Cap present rate (CPU) |
| `--hz` / `hz` | Best-effort display refresh hint |
| `--stats` | Start with on-screen stats HUD (also toggle with `i`) |
| `--auto-hide` / `auto_hide` | Kiosk: hide chrome + cursor after idle (`auto_hide_ms`, default 4000) |
| `--protocol` / `protocol` | `ghost_ndihx` · `srt` · `rtmp` · `rtsp` |
| `--url` / `url` | Full URL for SRT/RTMP/RTSP receive |

### Alpine Pi appliance (decode-only HDMI)

Flashable Alpine **aarch64** image for Pi **3B+ / 4 / 5**: HDMI kiosk + LAN UI `:8080`.

See **[docs/alpine-pi-appliance.md](docs/alpine-pi-appliance.md)** and
**[packaging/alpine-pi/README.md](packaging/alpine-pi/README.md)**.

```bash
# On Linux aarch64 (e.g. booth 172.16.1.144):
sudo ./packaging/alpine-pi/build-image.sh
```

| Key | Action |
| --- | --- |
| `c` | Toggle light **controls** overlay (video keeps playing; no dim) |
| `i` | Toggle **stats** HUD (resolution, src/present fps, bandwidth, source) |
| `q` / Esc | Quit |
| `f` | Fullscreen |
| Space | Pause |
| `r` | Rescan / reconnect |
| `[` / `]` | Bandwidth lowest / highest (when module supports it) |
| `-` / `=` | FPS cap step · `0` uncapped |

Controls overlay also exposes auto-search, max W/H, rescan, stats checkbox, and a
**PTZ pad** only when the active module reports `MEDIA_CAP_PTZ` (NDI\|HX probes
`NDIlib_recv_ptz_is_supported` after connect). Pure receive sources show no PTZ chrome.

## Latency & leaks

- Capture path always **drains to the newest frame** and frees intermediates
  (also continues past NDI `status_change` so PTZ/capability events cannot strand
  stale frames). `ghost_ndihx_drain()` discards the queue when the host is paused.
- Audio/metadata are not pulled (lower overhead for video-only monitors).
- Default bandwidth on the NDI\|HX module is **highest** (full sender resolution,
  BGRX). `lowest` is an explicit opt-in — never a silent quality drop.
- Vsync off in GhostVidStream; optional fps-cap when you want less CPU.
- Tear down with `ghost_ndihx_session_destroy` (frees last frame + receiver + finder).

## Resource usage

Steady-state CPU / RSS / threads on the aarch64 booth host (no stream · low ·
max quality · HUD overlays): **[docs/resource-usage.md](docs/resource-usage.md)**.

SRT / RTMP decoder bring-up vs AIDA camera:
**[docs/srt-rtmp-decoder.md](docs/srt-rtmp-decoder.md)**.

## Embed / modules

- **NDI\|HX only:** **[docs/integration.md](docs/integration.md)** (`libghost_ndihx`)
- **Pluggable shell contract:** **[docs/modular-compatibility.md](docs/modular-compatibility.md)**

```c
#include <ghost_ndihx.h>
/* First module today: ghost_ndihx_session_create → connect_auto → capture_newest */
/* Pause path: ghost_ndihx_drain(session) so the receive queue cannot grow */
```

Link (NDI\|HX module): `-lghost_ndihx -lmedia_core -lndi -ldl -lpthread -lm`
(plus rpath to `PREFIX/lib`).

## Architectures

`install-deps.sh` and the Makefile are arch-portable:

- **aarch64 / arm64** → NDI `lib/aarch64-rpi4-linux-gnueabi` (HX module)
- **x86_64 / amd64** → NDI `lib/x86_64-linux-gnu`

No arch-hardcoded paths in the build. Future modules may add their own deps.

## Layout

```
include/media_core.h             Protocol-agnostic module API + PTZ caps
include/ghost_ndihx.h            NDI|HX native API (first decoder plugin)
src/core/media_core.c            Module registry (no protocol SDKs)
src/modules/ghost_ndihx/         NDI|HX implementation + PTZ probe
src/modules/{ndi_full,st2110,rtsp}/  Planned protocol modules (placeholders)
src/viewer_main.c                GhostVidStream SDL shell (overlays / HUD)
assets/ghostvidstream.png        Desktop icon (512px)
packaging/ghostvidstream.desktop
docs/integration.md              NDI|HX embed guide
docs/modular-compatibility.md    Pluggable shell contract
config.example.conf              Sample settings
install-deps.sh                  System deps (aarch64 + x86_64)
Makefile
```

## Remotes

- GitHub (public): https://github.com/iHadAThought/GhostVidStream
- Forgejo (private): https://git.ghostnetwork.app/Brendan/GhostVidStream
- Decoder library: https://github.com/iHadAThought/libghost_ndihx
- BookStack: https://bookstack.ghostnetwork.app/books/ghostvidstream
