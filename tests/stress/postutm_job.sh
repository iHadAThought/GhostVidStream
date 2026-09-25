#!/usr/bin/env bash
# Continue remaining suites; do not abort on individual FAIL (set +e around suites).
set -uo pipefail
cd /opt/ndi-for-linux
export DISPLAY=:0
XA=$(ls /run/user/1000/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)
export XAUTHORITY="$XA" LD_LIBRARY_PATH=/usr/local/lib
export NDI_STRESS_IP=172.16.1.189
export NDI_STRESS_LOG_DIR=/tmp/ndi-stress
export NDI_STRESS_LIST_ROUNDS=15
export NDI_STRESS_ASAN_SEC=25
export NDI_RESOURCE_DIR=/tmp/ndi-resource
export NDI_RESOURCE_WARMUP=8
export NDI_RESOURCE_SAMPLE=12
export NDI_STRESS_VIEWER=/usr/local/bin/ghostvidstream

mkdir -p "$NDI_STRESS_LOG_DIR" "$NDI_RESOURCE_DIR"
LOG="$NDI_STRESS_LOG_DIR/postutm.log"
exec >> >(tee -a "$LOG") 2>&1

BIN=./stress_ghost_ndihx
run() {
  local name="$1"; shift
  echo "=== RUN $name $(date -Is) ==="
  set +e
  "$@" | tee "$NDI_STRESS_LOG_DIR/${name}.out"
  local rc=${PIPESTATUS[0]}
  set -e
  echo "=== END $name rc=$rc ==="
}

echo "=== post-UTM continue $(date -Is) ==="
# Retry reconnect (prior flake 14/15)
run lib_reconnect_retry $BIN --ip "$NDI_STRESS_IP" --suite reconnect --reconnect-n 20
run lib_bw $BIN --ip "$NDI_STRESS_IP" --suite bw --bw-flaps 12
run lib_ptz $BIN --ip "$NDI_STRESS_IP" --suite ptz --ptz-cycles 20
run lib_multi $BIN --ip "$NDI_STRESS_IP" --suite multi --multi-sec 15
run lib_soak $BIN --ip "$NDI_STRESS_IP" --suite soak --soak-sec 60
run list_churn bash tests/stress/list_churn.sh
run asan bash tests/stress/run_asan.sh
run valgrind bash tests/stress/run_valgrind.sh

echo "=== resource profile $(date -Is) ==="
set +e
bash tests/stress/resource_profile.sh | tee "$NDI_RESOURCE_DIR/run.log"
echo "resource_rc=${PIPESTATUS[0]}"
set -e

echo "=== DONE $(date -Is) ==="
echo EXIT=0
