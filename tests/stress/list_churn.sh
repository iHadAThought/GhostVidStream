#!/usr/bin/env bash
# Run CLI discovery churn against a live camera (no GUI).
set -euo pipefail
IP="${NDI_STRESS_IP:-172.16.1.189}"
ROUNDS="${NDI_STRESS_LIST_ROUNDS:-30}"
FIND_MS="${NDI_STRESS_FIND_MS:-3000}"
BIN="${NDI_STRESS_VIEWER:-/usr/local/bin/ghostvidstream}"
LOG="${NDI_STRESS_LOG_DIR:-/tmp/ndi-stress}/list_churn.log"
mkdir -p "$(dirname "$LOG")"

hits=0
misses=0
echo "list_churn rounds=$ROUNDS ip=$IP" | tee "$LOG"
for i in $(seq 1 "$ROUNDS"); do
  out="$("$BIN" --list --ip "$IP" --find-ms "$FIND_MS" 2>&1 || true)"
  if echo "$out" | grep -q "$IP"; then
    hits=$((hits + 1))
    echo "round $i HIT" | tee -a "$LOG"
  else
    misses=$((misses + 1))
    echo "round $i MISS" | tee -a "$LOG"
    echo "$out" | tee -a "$LOG"
  fi
done
echo "METRIC	list_churn	hits	$hits"
echo "METRIC	list_churn	misses	$misses"
if (( hits * 2 >= ROUNDS && hits > 0 )); then
  echo "RESULT	list_churn	PASS	hits=$hits misses=$misses"
  exit 0
fi
echo "RESULT	list_churn	FAIL	hits=$hits misses=$misses"
exit 1
