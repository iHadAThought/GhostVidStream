#!/usr/bin/env bash
# Short Valgrind run — classify third-party (libndi/FFmpeg) noise separately.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IP="${NDI_STRESS_IP:-172.16.1.189}"
LOG_DIR="${NDI_STRESS_LOG_DIR:-/tmp/ndi-stress}"
mkdir -p "$LOG_DIR"
BIN="${ROOT}/stress_ghost_ndihx"
[[ -x "$BIN" ]] || BIN="/opt/ndi-for-linux/stress_ghost_ndihx"
if ! command -v valgrind >/dev/null 2>&1; then
  echo "RESULT	valgrind	SKIP	valgrind not installed"
  exit 0
fi
if [[ ! -x "$BIN" ]]; then
  echo "RESULT	valgrind	SKIP	stress binary missing"
  exit 0
fi

set +e
timeout "${NDI_STRESS_VALGRIND_TIMEOUT:-180}" valgrind \
  --error-exitcode=99 \
  --leak-check=no \
  --track-origins=no \
  "$BIN" --ip "$IP" --suite reconnect --reconnect-n 5 --find-ms 3000 \
  >"$LOG_DIR/valgrind.log" 2>&1
rc=$?
set -e

harness_ok=0
grep -q $'RESULT\treconnect\tPASS' "$LOG_DIR/valgrind.log" && harness_ok=1

# Stack frames that are not clearly vendor / runtime.
# (Valgrind "Invalid read" lines themselves never name the .so — check "at 0x" frames.)
ours=$(grep -E '^\s+at 0x' "$LOG_DIR/valgrind.log" \
  | grep -vE 'libndi\.so|libavcodec|libavutil|libavformat|libavresample|vg_replace_malloc|pthread_create|clone\.S|start_thread|libc\.so' \
  | head -10 || true)

errors=$(grep -E 'ERROR SUMMARY:' "$LOG_DIR/valgrind.log" | tail -1 || true)

if [[ "$harness_ok" -eq 1 && -z "$ours" ]]; then
  echo "METRIC	valgrind	harness_ok	1"
  echo "RESULT	valgrind	PASS	reconnect ok; Valgrind hits only in libndi/FFmpeg ($errors)"
  exit 0
fi
if [[ $rc -eq 0 && -z "$ours" ]]; then
  echo "RESULT	valgrind	PASS	clean"
  exit 0
fi
echo "RESULT	valgrind	FAIL	rc=$rc harness_ok=$harness_ok"
echo "$ours"
tail -40 "$LOG_DIR/valgrind.log" || true
exit 1
