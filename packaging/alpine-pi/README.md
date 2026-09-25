# Alpine Pi GhostVidStream appliance (decode-only → HDMI)

Flashable **Alpine Linux aarch64** image for Raspberry Pi **3B+ / 4 / 5**. Boots to an HDMI GhostVidStream kiosk (keyboard/mouse setup, auto-hiding chrome) plus a minimal LAN settings UI on **:8080**.

## Locked product rules

- **Decode only** — no encoders, publishers, or transcoders in the image
- **SDL2 KMSDRM** preferred (no full desktop)
- **OpenRC** init
- Pi **3B+** defaults to `bandwidth=lowest`; Pi **4/5** to `highest`
- SSH **disabled by default** (key-only policy file present if you enable `sshd`)

## Build (preferred: Linux aarch64)

On booth host `172.16.1.144` or any aarch64 Linux with root, from a GhostVidStream checkout:

```bash
cd /path/to/GhostVidStream   # e.g. /opt/ndi-for-linux
sudo ./packaging/alpine-pi/build-image.sh
```

Artifacts:

```text
packaging/alpine-pi/out/ghostvidstream-alpine-pi-aarch64-YYYYmmdd.img.xz
packaging/alpine-pi/out/ghostvidstream-alpine-pi-aarch64-YYYYmmdd.img.xz.sha256
```

Optional env:

| Variable | Default | Meaning |
| --- | --- | --- |
| `ALPINE_VERSION` | `3.20` | Alpine release |
| `IMAGE_MB` | `3072` | Raw image size before xz |
| `SKIP_NDI` | `0` | `1` = URL protocols only (no NDI SDK fetch) |
| `KEEP_RAW_IMG` | `0` | `1` = keep uncompressed `.img` |
| `GVS_SRC` | repo root | Source tree to bake |

**macOS:** this script refuses non-Linux hosts. Sync the tree to `172.16.1.144` and run there (or on a Pi).

## Flash

```bash
xz -dc ghostvidstream-alpine-pi-aarch64-YYYYmmdd.img.xz \
  | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
```

Or use Raspberry Pi Imager → custom image (decompress `.xz` first if the Imager build requires raw `.img`).

## First boot

1. Ethernet DHCP → note the address (Avahi hostname `ghostvidstream.local` when mDNS works)
2. HDMI shows GhostVidStream fullscreen (searching until a source matches)
3. Keyboard/mouse: `c` controls, `i` stats, `f` fullscreen, `q` quit (service respawns); chrome **auto-hides** after ~4s idle
4. LAN UI: `http://<ip>:8080/` — protocol / URL / IP / bandwidth / apply+restart / reboot

Config on disk: `/etc/ghostvidstream/config`  
Model profiles: `/etc/ghostvidstream/profiles/` (applied by `gvs-apply-profile`)

## Enable SSH (optional)

```sh
# as root on the appliance (serial/HDMI shell)
install -d -m 700 /home/gvs/.ssh   # or use a login-capable admin user you create
# install YOUR pubkey to authorized_keys (never commit private keys)
rc-update add sshd default
rc-service sshd start
```

Password auth stays off (`/etc/ssh/sshd_config.d/99-gvs-appliance.conf`).

## Layout

```text
packaging/alpine-pi/
  build-image.sh
  packages.txt
  README.md
  rootfs-overlay/
    etc/ghostvidstream/…
    etc/init.d/ghostvidstream{,-web}
    usr/local/bin/ghostvidstream-web
    usr/local/bin/gvs-apply-profile
  out/   # build artifacts (gitignored)
```

## NDI note

NDI Advanced SDK ships **glibc** `.so` files. The appliance installs Alpine **`gcompat`** and places `libndi` under `/usr/local/lib`. SRT/RTMP/RTSP use Alpine **ffmpeg** + **libsrt** (musl) and do not need the NDI SDK (`SKIP_NDI=1`).
