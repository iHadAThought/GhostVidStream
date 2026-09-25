#!/usr/bin/env bash
# Master stress runner for NDI for Linux on the booth host.
# Protects the GNOME fullscreen viewer: pauses tmux ndi-viewer during UI thrash,
# keeps library tests in a separate process (second NDI receiver is OK).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

export NDI_STRESS_IP="${NDI_STRESS_IP:-172.16.1.189}"
export NDI_STRESS_LOG_DIR="${NDI_STRESS_LOG_DIR:-/tmp/ndi-stress}"
export NDI_STRESS_VIEWER="${NDI_STRESS_VIEWER:-/usr/local/bin/ghostvidstream}"
mkdir -p "$NDI_STRESS_LOG_DIR"
OUT_SUMMARY="$NDI_STRESS_LOG_DIR/summary.txt"
: >"$OUT_SUMMARY"

pass=0
fail=0
skip=0

record() {
  local line="$1"
  echo "$line" | tee -a "$OUT_SUMMARY"
  if [[ "$line" == RESULT$'\t'* ]]; then
    local status
    status="$(echo "$line" | cut -f3)"
    case "$status" in
      PASS) pass=$((pass + 1)) ;;
      FAIL) fail=$((fail + 1)) ;;
      SKIP) skip=$((skip + 1)) ;;
    esac
  fi
}

run_capture() {
  local name="$1"
  shift
  local log="$NDI_STRESS_LOG_DIR/${name}.out"
  echo "=== RUN $name ===" | tee -a "$OUT_SUMMARY"
  set +e
  "$@" >"$log" 2>&1
  local rc=$?
  set -e
  while IFS= read -r line; do
    record "$line"
  done < <(grep -E $'^(METRIC|RESULT)\t' "$log" || true)
  if ! grep -q $'^RESULT\t' "$log"; then
    if [[ $rc -eq 0 ]]; then
      record $'RESULT\t'"$name"$'\tPASS\t(no RESULT line, rc=0)'
    else
      record $'RESULT\t'"$name"$'\tFAIL\trc='"$rc"
      tail -30 "$log" | tee -a "$OUT_SUMMARY" || true
    fi
  fi
  return 0
}

BIN="$ROOT/stress_ghost_ndihx"
if [[ ! -x "$BIN" ]]; then
  echo "Building stress_ghost_ndihx…"
  make -C "$ROOT" stress
fi

echo "stress run $(date -Is) ip=$NDI_STRESS_IP host=$(hostname)" | tee -a "$OUT_SUMMARY"

SOAK="${NDI_STRESS_SOAK_SEC:-180}"
run_capture lib_wrongip "$BIN" --ip "$NDI_STRESS_IP" --suite wrongip
run_capture lib_discover "$BIN" --ip "$NDI_STRESS_IP" --suite discover --discover-n "${NDI_STRESS_DISCOVER_N:-20}"
run_capture lib_reconnect "$BIN" --ip "$NDI_STRESS_IP" --suite reconnect --reconnect-n "${NDI_STRESS_RECONNECT_N:-40}"
run_capture lib_bw "$BIN" --ip "$NDI_STRESS_IP" --suite bw --bw-flaps "${NDI_STRESS_BW_FLAPS:-30}"
run_capture lib_ptz "$BIN" --ip "$NDI_STRESS_IP" --suite ptz --ptz-cycles "${NDI_STRESS_PTZ_CYCLES:-40}"
run_capture lib_multi "$BIN" --ip "$NDI_STRESS_IP" --suite multi --multi-sec "${NDI_STRESS_MULTI_SEC:-20}"
run_capture lib_soak "$BIN" --ip "$NDI_STRESS_IP" --suite soak --soak-sec "$SOAK"

run_capture list_churn bash "$ROOT/tests/stress/list_churn.sh"
run_capture cpu_load bash "$ROOT/tests/stress/cpu_load_soak.sh"
run_capture asan bash "$ROOT/tests/stress/run_asan.sh"
run_capture valgrind bash "$ROOT/tests/stress/run_valgrind.sh"

booth_was_up=0
if tmux has-session -t ndi-viewer 2>/dev/null; then
  booth_was_up=1
  echo "Pausing tmux ndi-viewer for UI thrash…" | tee -a "$OUT_SUMMARY"
  tmux kill-session -t ndi-viewer 2>/dev/null || true
  pkill -x ghostvidstream 2>/dev/null || true
  sleep 1
fi

export DISPLAY="${DISPLAY:-:0}"
if [[ -z "${XAUTHORITY:-}" ]]; then
  xa="$(ls /run/user/"${UID:-1000}"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)"
  [[ -n "$xa" ]] && export XAUTHORITY="$xa"
fi
run_capture ui_thrash bash "$ROOT/tests/stress/ui_thrash.sh"

if [[ "$booth_was_up" -eq 1 ]]; then
  echo "Restoring tmux ndi-viewer…" | tee -a "$OUT_SUMMARY"
  xa="${XAUTHORITY:-}"
  tmux new-session -d -s ndi-viewer \
    "env DISPLAY=${DISPLAY:-:0} XAUTHORITY=${xa} LD_LIBRARY_PATH=/usr/local/lib \
     $NDI_STRESS_VIEWER --auto --stats --fullscreen --find-ms 5000 \
     2>&1 | tee /tmp/ndi-viewer.log"
  sleep 3
  if pgrep -x ghostvidstream >/dev/null; then
    record $'RESULT\tbooth_restore\tPASS\tndi-viewer restarted'
  else
    record $'RESULT\tbooth_restore\tFAIL\tviewer not running'
  fi
fi

echo "TOTALS	pass=$pass fail=$fail skip=$skip" | tee -a "$OUT_SUMMARY"
[[ "$fail" -eq 0 ]]
