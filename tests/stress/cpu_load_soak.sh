#!/usr/bin/env bash
# Decode under CPU load (stress-ng) while capturing.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IP="${NDI_STRESS_IP:-172.16.1.189}"
LOG_DIR="${NDI_STRESS_LOG_DIR:-/tmp/ndi-stress}"
SEC="${NDI_STRESS_CPU_SEC:-60}"
mkdir -p "$LOG_DIR"
BIN="${ROOT}/stress_ghost_ndihx"
[[ -x "$BIN" ]] || BIN="/opt/ndi-for-linux/stress_ghost_ndihx"
[[ -x "$BIN" ]] || BIN="./stress_ghost_ndihx"

if ! command -v stress-ng >/dev/null 2>&1; then
  echo "RESULT	cpu_load	SKIP	stress-ng not installed"
  exit 0
fi
if [[ ! -x "$BIN" ]]; then
  echo "RESULT	cpu_load	SKIP	stress binary missing"
  exit 0
fi

nproc="$(nproc)"
# Leave one core for the decoder.
workers=$((nproc > 1 ? nproc - 1 : 1))
stress-ng --cpu "$workers" --timeout "${SEC}s" >"$LOG_DIR/stress-ng.log" 2>&1 &
sp=$!
cleanup() { kill "$sp" 2>/dev/null || true; wait "$sp" 2>/dev/null || true; }
trap cleanup EXIT

set +e
"$BIN" --ip "$IP" --suite soak --soak-sec "$SEC" --find-ms 4000 | tee "$LOG_DIR/cpu_load.log"
rc=${PIPESTATUS[0]}
set -e
cleanup
trap - EXIT

if [[ $rc -eq 0 ]]; then
  echo "RESULT	cpu_load	PASS	soak under stress-ng cpu=$workers"
  exit 0
fi
echo "RESULT	cpu_load	FAIL	rc=$rc"
exit 1
