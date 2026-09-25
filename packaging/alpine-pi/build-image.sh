#!/usr/bin/env bash
# Build a flashable Alpine aarch64 GhostVidStream decode-only appliance image.
#
# Preferred host: Linux aarch64 (booth 172.16.1.144 or a Pi).
# Requires: root (or passwordless sudo), losetup, sfdisk/parted, mkfs.vfat, mkfs.ext4,
#           curl/wget, tar, xz, chroot.
#
# Exact command (on aarch64 Linux, from GhostVidStream checkout):
#   sudo ./packaging/alpine-pi/build-image.sh
# Optional:
#   sudo GVS_SRC=/path/to/ndi-for-linux ALPINE_VERSION=3.20 ./packaging/alpine-pi/build-image.sh
#
# Outputs under packaging/alpine-pi/out/:
#   ghostvidstream-alpine-pi-aarch64-YYYYmmdd.img.xz
#   ghostvidstream-alpine-pi-aarch64-YYYYmmdd.img.xz.sha256
#
# Decode-only: no encoder/publisher packages. NDI SDK (glibc) runs via gcompat.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
OUT_DIR="${OUT_DIR:-${SCRIPT_DIR}/out}"
WORK_DIR="${WORK_DIR:-${OUT_DIR}/work}"
OVERLAY="${SCRIPT_DIR}/rootfs-overlay"
PACKAGES_FILE="${SCRIPT_DIR}/packages.txt"
ALPINE_VERSION="${ALPINE_VERSION:-3.20}"
ALPINE_MIRROR="${ALPINE_MIRROR:-https://dl-cdn.alpinelinux.org/alpine}"
IMAGE_MB="${IMAGE_MB:-3072}"
HOSTNAME_APPLIANCE="${HOSTNAME_APPLIANCE:-ghostvidstream}"
GVS_SRC="${GVS_SRC:-${REPO_ROOT}}"
SKIP_NDI="${SKIP_NDI:-0}"
NDI_INSTALLER_URL="${NDI_INSTALLER_URL:-https://downloads.ndi.tv/SDK/NDI_SDK_Linux/Install_NDI_SDK_v6_Linux.tar.gz}"

log() { printf '[build-image] %s\n' "$*"; }
die() { printf '[build-image] ERROR: %s\n' "$*" >&2; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "missing command: $1"; }

resolve_minirootfs() {
  # Prefer exact patch if provided; else probe common patch levels for ALPINE_VERSION.
  local ver="$1" mirror="$2" cache="$3"
  if [[ -n "${ALPINE_MINIROOT_URL:-}" ]]; then
    echo "$ALPINE_MINIROOT_URL"
    return 0
  fi
  local patch url
  for patch in 3 2 1 0; do
    url="${mirror}/v${ver}/releases/aarch64/alpine-minirootfs-${ver}.${patch}-aarch64.tar.gz"
    if curl -fsI -o /dev/null "$url"; then
      echo "$url"
      return 0
    fi
  done
  # Fall back to versioned path without probing success (caller will fail clearly)
  echo "${mirror}/v${ver}/releases/aarch64/alpine-minirootfs-${ver}.0-aarch64.tar.gz"
}

if [[ "$(id -u)" -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    exec sudo -E env "PATH=$PATH" bash "$0" "$@"
  fi
  die "run as root (or with sudo)"
fi

arch="$(uname -m)"
case "$arch" in
  aarch64|arm64) ;;
  *)
    die "prefer native aarch64 Linux (got ${arch}). Cross-build from Mac is not supported by this script; run on 172.16.1.144 or a Pi."
    ;;
esac
[[ "$(uname -s)" == "Linux" ]] || die "Linux required"

need curl; need tar; need xz; need losetup; need sfdisk; need mkfs.vfat; need mkfs.ext4
need chroot; need sha256sum; need install

STAMP="$(date -u +%Y%m%d)"
IMG_NAME="ghostvidstream-alpine-pi-aarch64-${STAMP}.img"
IMG="${OUT_DIR}/${IMG_NAME}"
IMG_XZ="${IMG}.xz"
ROOTFS="${WORK_DIR}/rootfs"
BOOT_MNT="${WORK_DIR}/mnt-boot"
ROOT_MNT="${WORK_DIR}/mnt-root"
CACHE="${WORK_DIR}/cache"
MINIROOT_URL="$(resolve_minirootfs "$ALPINE_VERSION" "$ALPINE_MIRROR" "$CACHE")"
MINIROOT_TGZ="${CACHE}/$(basename "$MINIROOT_URL")"

cleanup() {
  set +e
  if [[ -n "${LOOPDEV:-}" ]]; then
    sync
    umount "${BOOT_MNT}" 2>/dev/null
    umount "${ROOT_MNT}" 2>/dev/null
    losetup -d "${LOOPDEV}" 2>/dev/null
  fi
}
trap cleanup EXIT

mkdir -p "$OUT_DIR" "$WORK_DIR" "$CACHE" "$ROOTFS" "$BOOT_MNT" "$ROOT_MNT"

log "Alpine v${ALPINE_VERSION} aarch64 decode-only image → ${IMG_XZ}"

if [[ ! -f "$MINIROOT_TGZ" ]]; then
  log "Downloading Alpine minirootfs…"
  curl -fL --retry 3 -o "${MINIROOT_TGZ}.partial" "$MINIROOT_URL" \
    || die "download failed: ${MINIROOT_URL} (adjust ALPINE_VERSION if mirror path differs)"
  mv "${MINIROOT_TGZ}.partial" "$MINIROOT_TGZ"
fi

rm -rf "$ROOTFS"
mkdir -p "$ROOTFS"
tar -xzf "$MINIROOT_TGZ" -C "$ROOTFS"

# DNS + apk repos inside rootfs
cp /etc/resolv.conf "${ROOTFS}/etc/resolv.conf"
cat >"${ROOTFS}/etc/apk/repositories" <<EOF
${ALPINE_MIRROR}/v${ALPINE_VERSION}/main
${ALPINE_MIRROR}/v${ALPINE_VERSION}/community
EOF

pkgs=()
while IFS= read -r line || [[ -n "$line" ]]; do
  line="${line%%#*}"
  line="$(echo "$line" | tr -d '[:space:]')"
  [[ -z "$line" ]] && continue
  pkgs+=("$line")
done <"$PACKAGES_FILE"
[[ ${#pkgs[@]} -gt 0 ]] || die "empty packages.txt"

log "apk update + install (${#pkgs[@]} packages)…"
chroot "$ROOTFS" /bin/sh -c "apk update && apk add --no-cache ${pkgs[*]}"

# OpenRC defaults
chroot "$ROOTFS" /bin/sh -c '
  rc-update add devfs sysinit
  rc-update add dmesg sysinit
  rc-update add mdev sysinit
  rc-update add hwdrivers sysinit
  rc-update add networking boot
  rc-update add hostname boot
  rc-update add hwclock boot
  rc-update add modules boot
  rc-update add sysctl boot
  rc-update add local default
  rc-update add dbus default
  rc-update add chronyd default
  rc-update add dhcpcd default
  rc-update add avahi-daemon default
  # SSH off by default
  rc-update del sshd default 2>/dev/null || true
'

# Hostname
echo "$HOSTNAME_APPLIANCE" >"${ROOTFS}/etc/hostname"
cat >"${ROOTFS}/etc/hosts" <<EOF
127.0.0.1	localhost ${HOSTNAME_APPLIANCE}
::1		localhost ${HOSTNAME_APPLIANCE}
EOF

# Overlay (configs, OpenRC, web UI)
log "Applying rootfs-overlay…"
[[ -d "$OVERLAY/etc/init.d" ]] || die "overlay missing: $OVERLAY"
cp -a "${OVERLAY}/." "${ROOTFS}/"
test -f "${ROOTFS}/etc/init.d/ghostvidstream" || die "overlay did not install etc/init.d/ghostvidstream"
chmod 755 "${ROOTFS}/etc/init.d/ghostvidstream" \
  "${ROOTFS}/etc/init.d/ghostvidstream-web" \
  "${ROOTFS}/etc/local.d/ghostvidstream.start" \
  "${ROOTFS}/usr/local/bin/gvs-apply-profile" \
  "${ROOTFS}/usr/local/bin/ghostvidstream-web" \
  "${ROOTFS}/usr/local/bin/ghostvidstream-kiosk"
chroot "$ROOTFS" /bin/sh -c '
  rc-update add ghostvidstream default
  rc-update add ghostvidstream-web default
  rc-update add local default
'

# Build / install GhostVidStream into rootfs (decode binaries only)
log "Staging GhostVidStream sources…"
mkdir -p "${ROOTFS}/opt/ghostvidstream-src"
# Copy sources (exclude heavy/local artifacts)
tar -C "$GVS_SRC" \
  --exclude .git --exclude '*.o' --exclude '*.a' --exclude ghostvidstream \
  --exclude ndi-hx-viewer --exclude stress_url_rx --exclude stress_url_rx-asan \
  --exclude 'packaging/alpine-pi/out' \
  -cf - . | tar -C "${ROOTFS}/opt/ghostvidstream-src" -xf -

# Optional NDI SDK (glibc) under /opt/ndi — never dump into musl's default
# /usr/local/lib or apk/chroot breaks with "Error relocating /sbin/apk".
# Runtime: gcompat + DT_RUNPATH=/opt/ndi/lib.
if [[ "$SKIP_NDI" != "1" ]]; then
  log "Installing NDI SDK into /opt/ndi (glibc; runtime via gcompat)…"
  install -d "${ROOTFS}/opt/ndi/lib" "${ROOTFS}/opt/ndi/include"
  ndi_lib_src=""
  ndi_inc_src=""
  # Prefer host booth SDK if present (fast, already licensed/extracted).
  if [[ -f /usr/local/lib/libndi.so.6 ]] || [[ -f /usr/local/lib/libndi.so ]]; then
    ndi_lib_src=/usr/local/lib
    [[ -d /usr/local/include ]] && ndi_inc_src=/usr/local/include
    log "Using host NDI libs from ${ndi_lib_src}"
  else
    ndi_tgz="${CACHE}/Install_NDI_SDK_v6_Linux.tar.gz"
    if [[ ! -f "$ndi_tgz" ]]; then
      curl -fL --retry 3 -o "${ndi_tgz}.partial" "$NDI_INSTALLER_URL" || die "NDI SDK download failed (set SKIP_NDI=1 for URL-only image)"
      mv "${ndi_tgz}.partial" "$ndi_tgz"
    fi
    ndi_tmp="${WORK_DIR}/ndi-sdk"
    rm -rf "$ndi_tmp"; mkdir -p "$ndi_tmp"
    tar -xzf "$ndi_tgz" -C "$ndi_tmp"
    installer="$(find "$ndi_tmp" -type f -name 'Install_NDI_SDK_v6_Linux.sh' | head -n1)"
    [[ -n "$installer" ]] || die "NDI installer script not found in tarball"
    (cd "$(dirname "$installer")" && yes | sh "$(basename "$installer")" >/dev/null) || true
    ndi_root="$(find "$ndi_tmp" -type d -name 'NDI SDK for Linux' | head -n1 || true)"
    if [[ -z "$ndi_root" ]]; then
      ndi_root="$(find / -maxdepth 3 -type d -name 'NDI SDK for Linux' 2>/dev/null | head -n1 || true)"
    fi
    [[ -n "$ndi_root" ]] || die "NDI SDK tree not found after install"
    ndi_lib_src="${ndi_root}/lib/aarch64-rpi4-linux-gnueabi"
    [[ -d "$ndi_lib_src" ]] || ndi_lib_src="$(find "${ndi_root}/lib" -maxdepth 1 -type d -name 'aarch64*' | head -n1)"
    [[ -d "$ndi_lib_src" ]] || die "NDI aarch64 lib dir missing"
    [[ -d "${ndi_root}/include" ]] && ndi_inc_src="${ndi_root}/include"
  fi
  # Copy only NDI-related shared objects + headers (avoid polluting with unrelated host libs).
  shopt -s nullglob
  for f in "${ndi_lib_src}"/libndi.so*; do
    cp -a "$f" "${ROOTFS}/opt/ndi/lib/"
  done
  shopt -u nullglob
  [[ -e "${ROOTFS}/opt/ndi/lib/libndi.so" || -e "${ROOTFS}/opt/ndi/lib/libndi.so.6" ]] \
    || die "libndi.so* not found under ${ndi_lib_src}"
  if [[ -n "$ndi_inc_src" ]]; then
    # Prefer Processing.NDI.* headers if present; else copy whole include tree subset.
    if compgen -G "${ndi_inc_src}/Processing.NDI.*" >/dev/null; then
      cp -a "${ndi_inc_src}"/Processing.NDI.* "${ROOTFS}/opt/ndi/include/"
    else
      cp -a "${ndi_inc_src}/." "${ROOTFS}/opt/ndi/include/"
    fi
  fi
  # gcompat wrapper hint for kiosk env
  mkdir -p "${ROOTFS}/etc/profile.d"
  cat >"${ROOTFS}/etc/profile.d/gvs-ndi.sh" <<'EOF'
export LD_LIBRARY_PATH="/opt/ndi/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
EOF
fi

# Build tools in chroot for compile, then leave runtime libs
log "Building GhostVidStream inside chroot…"
chroot "$ROOTFS" /bin/sh -c '
  set -e
  apk add --no-cache build-base pkgconf sdl2-dev ffmpeg-dev libsrt-dev
  cd /opt/ghostvidstream-src
  export PKG_CONFIG_PATH=/usr/lib/pkgconfig
  export CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter -Iinclude -I/usr/local/include -I/opt/ndi/include"
  export LDFLAGS="-L/opt/ndi/lib -Wl,-rpath,/opt/ndi/lib -L/usr/local/lib -Wl,-rpath,/usr/local/lib"
  make clean || true
  make -j"$(nproc)" all
  make install PREFIX=/usr/local
  test -x /usr/local/bin/ghostvidstream
  # Drop build-only packages to keep appliance lean (decode runtime remains)
  apk del build-base pkgconf sdl2-dev ffmpeg-dev libsrt-dev || true
  # Keep runtime: sdl2, ffmpeg-libs, libsrt, gcompat
' || die "chroot GhostVidStream build failed"

# Ensure no accidental encoder-ish services
chroot "$ROOTFS" /bin/sh -c '
  for s in nginx apache2 docker containerd; do
    rc-update del "$s" default 2>/dev/null || true
  done
'

# Create sparse image + partitions (boot FAT + root ext4)
log "Creating ${IMAGE_MB} MiB disk image…"
rm -f "$IMG" "$IMG_XZ" "${IMG_XZ}.sha256"
dd if=/dev/zero of="$IMG" bs=1M count="$IMAGE_MB" status=none
sfdisk "$IMG" <<'EOF'
label: dos
unit: sectors
# boot: 256 MiB FAT32
1 : start=2048, size=524288, type=c, bootable
# root: remainder
2 : start=526336, type=83
EOF

LOOPDEV="$(losetup -f --show -P "$IMG")"
log "Loop device ${LOOPDEV}"
mkfs.vfat -F 32 -n GVSBOOT "${LOOPDEV}p1"
mkfs.ext4 -F -L gvsroot "${LOOPDEV}p2"
mount "${LOOPDEV}p1" "$BOOT_MNT"
mount "${LOOPDEV}p2" "$ROOT_MNT"

log "Populating root partition…"
tar -C "$ROOTFS" -cf - . | tar -C "$ROOT_MNT" -xf -

# Raspberry Pi boot files from linux-rpi / raspberrypi-bootloader packages
log "Populating boot partition…"
if [[ -d "${ROOT_MNT}/boot" ]]; then
  # Copy firmware + kernels that apk placed under /boot
  cp -a "${ROOT_MNT}/boot/." "$BOOT_MNT/" || true
fi
# Minimal config.txt / cmdline.txt for Pi 3B+/4/5 aarch64 HDMI kiosk
cat >"${BOOT_MNT}/config.txt" <<'EOF'
# GhostVidStream Alpine appliance
arm_64bit=1
disable_overscan=1
hdmi_force_hotplug=1
dtoverlay=vc4-kms-v3d
gpu_mem=128
# Pi 5 may ignore gpu_mem; harmless on 4/3B+
EOF
cat >"${BOOT_MNT}/cmdline.txt" <<'EOF'
modules=loop,squashfs,sd-mod,usb-storage quiet console=tty1 root=/dev/mmcblk0p2 rootfstype=ext4 fsck.repair=yes rootwait
EOF

# fstab
cat >"${ROOT_MNT}/etc/fstab" <<'EOF'
/dev/mmcblk0p2	/	ext4	rw,noatime	0 1
/dev/mmcblk0p1	/boot	vfat	rw,noatime	0 2
tmpfs	/tmp	tmpfs	nosuid,nodev,size=128m	0 0
tmpfs	/run	tmpfs	nosuid,nodev,size=64m	0 0
EOF

sync
umount "$BOOT_MNT"
umount "$ROOT_MNT"
losetup -d "$LOOPDEV"
LOOPDEV=""

log "Compressing with xz…"
xz -T0 -9 -f -k "$IMG"
mv -f "${IMG}.xz" "$IMG_XZ"
(cd "$OUT_DIR" && sha256sum "$(basename "$IMG_XZ")" >"$(basename "$IMG_XZ").sha256")

# Keep raw img optional (large); default remove to save disk
if [[ "${KEEP_RAW_IMG:-0}" != "1" ]]; then
  rm -f "$IMG"
fi

log "Done:"
log "  ${IMG_XZ}"
log "  ${IMG_XZ}.sha256"
log "Flash: xz -dc ${IMG_XZ} | sudo dd of=/dev/sdX bs=4M status=progress conv=fsync"
log "First boot: HDMI kiosk + http://<dhcp-ip>:8080/"
