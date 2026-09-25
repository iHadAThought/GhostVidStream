#!/usr/bin/env bash
# Overlay / key thrash on a temporary windowed viewer (not the booth fullscreen session).
# Requires DISPLAY + xdotool. Restores nothing itself — run_all.sh owns booth lifecycle.
set -euo pipefail
IP="${NDI_STRESS_IP:-172.16.1.189}"
SECONDS_RUN="${NDI_STRESS_UI_SEC:-45}"
BIN="${NDI_STRESS_VIEWER:-/usr/local/bin/ghostvidstream}"
LOG_DIR="${NDI_STRESS_LOG_DIR:-/tmp/ndi-stress}"
mkdir -p "$LOG_DIR"
LOG="$LOG_DIR/ui_thrash.log"

if ! command -v xdotool >/dev/null 2>&1; then
  echo "RESULT	ui_thrash	SKIP	xdotool not installed"
  exit 0
fi
if [[ -z "${DISPLAY:-}" ]]; then
  echo "RESULT	ui_thrash	SKIP	no DISPLAY"
  exit 0
fi

: >"$LOG"
# Windowed (not fullscreen) so we do not steal the whole booth screen.
env LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/local/lib}" \
  "$BIN" --ip "$IP" --auto --stats --find-ms 4000 \
  >"$LOG" 2>&1 &
pid=$!
cleanup() {
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
}
trap cleanup EXIT

# Wait for window
wid=""
for _ in $(seq 1 40); do
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "RESULT	ui_thrash	FAIL	viewer exited early"
    tail -30 "$LOG" || true
    exit 1
  fi
  wid="$(xdotool search --pid "$pid" --class '' 2>/dev/null | tail -1 || true)"
  if [[ -z "$wid" ]]; then
    wid="$(xdotool search --name 'HX-Stream\|ndi-hx\|HD-NDI' 2>/dev/null | tail -1 || true)"
  fi
  [[ -n "$wid" ]] && break
  sleep 0.25
done
if [[ -z "$wid" ]]; then
  # Fallback: any window belonging to process via pgrep mapping
  sleep 2
  wid="$(xdotool search --pid "$pid" 2>/dev/null | tail -1 || true)"
fi
if [[ -z "$wid" ]]; then
  echo "RESULT	ui_thrash	FAIL	no window id"
  tail -40 "$LOG" || true
  exit 1
fi

xdotool windowactivate --sync "$wid" || true
end=$((SECONDS + SECONDS_RUN))
keys=0
while (( SECONDS < end )); do
  # c=controls, i=stats, [/]=bw, -/= fps, 0 uncapped, f fullscreen toggle briefly
  for k in c i c bracketleft bracketright minus equal 0 f f; do
    xdotool key --window "$wid" "$k" || true
    keys=$((keys + 1))
    sleep 0.05
  done
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "METRIC	ui_thrash	keys	$keys"
    echo "RESULT	ui_thrash	FAIL	viewer crashed mid-thrash"
    tail -50 "$LOG" || true
    exit 1
  fi
done

# Confirm still alive and saw frames
if ! kill -0 "$pid" 2>/dev/null; then
  echo "RESULT	ui_thrash	FAIL	viewer dead at end"
  exit 1
fi
if ! grep -q 'First frame' "$LOG"; then
  echo "RESULT	ui_thrash	FAIL	no first frame"
  tail -40 "$LOG" || true
  exit 1
fi
echo "METRIC	ui_thrash	keys	$keys"
echo "METRIC	ui_thrash	sec	$SECONDS_RUN"
echo "RESULT	ui_thrash	PASS	keys=$keys alive"
exit 0
