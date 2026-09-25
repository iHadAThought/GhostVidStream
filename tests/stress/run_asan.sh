#!/usr/bin/env bash
# Short ASan capture path (build stress_ghost_ndihx-asan if present).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
IP="${NDI_STRESS_IP:-172.16.1.189}"
LOG_DIR="${NDI_STRESS_LOG_DIR:-/tmp/ndi-stress}"
mkdir -p "$LOG_DIR"
BIN="${ROOT}/stress_ghost_ndihx-asan"
if [[ ! -x "$BIN" ]]; then
  echo "RESULT	asan	SKIP	binary missing ($BIN)"
  exit 0
fi
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:halt_on_error=1}"
set +e
"$BIN" --ip "$IP" --suite soak --soak-sec "${NDI_STRESS_ASAN_SEC:-25}" --find-ms 4000 \
  | tee "$LOG_DIR/asan.log"
rc=${PIPESTATUS[0]}
set -e
if [[ $rc -eq 0 ]]; then
  echo "RESULT	asan	PASS	soak under ASan"
  exit 0
fi
echo "RESULT	asan	FAIL	rc=$rc"
exit 1
