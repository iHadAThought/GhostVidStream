# Alpine Pi GhostVidStream appliance

Decode-only Alpine Linux **aarch64** image for Raspberry Pi **3B+ and newer**. Boots to HDMI GhostVidStream (SDL2 KMSDRM kiosk) plus LAN settings on **:8080**.

Full packaging notes: [`packaging/alpine-pi/README.md`](../packaging/alpine-pi/README.md).

## Build

On Linux **aarch64** (booth `172.16.1.144` or a Pi):

```bash
cd /path/to/GhostVidStream
sudo ./packaging/alpine-pi/build-image.sh
```

Produces `packaging/alpine-pi/out/ghostvidstream-alpine-pi-aarch64-*.img.xz` (+ `.sha256`).

## Flash / first boot

```bash
xz -dc ghostvidstream-alpine-pi-aarch64-*.img.xz \
  | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync
```

- HDMI kiosk auto-starts; chrome auto-hides after idle
- Web UI: `http://<dhcp-ip>:8080/` (receive settings only)
- Pi **3B+**: `bandwidth=lowest` by default; Pi **4/5**: `highest`
- SSH disabled by default

## Out of scope

Encoders/publishers, full desktop, Pi 3B (non-plus) / 32-bit images.
