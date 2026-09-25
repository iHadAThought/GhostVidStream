#!/usr/bin/env bash
# Resource profile for ghostvidstream / decoder on the booth host.
# Samples steady-state CPU/RSS/threads/fds after warmup for several scenarios.
set -euo pipefail

IP="${NDI_STRESS_IP:-172.16.1.189}"
BIN="${NDI_STRESS_VIEWER:-/usr/local/bin/ghostvidstream}"
OUT_DIR="${NDI_RESOURCE_DIR:-/tmp/ndi-resource}"
WARMUP="${NDI_RESOURCE_WARMUP:-8}"
SAMPLE_SEC="${NDI_RESOURCE_SAMPLE:-12}"
mkdir -p "$OUT_DIR"
CSV="$OUT_DIR/profile.csv"
RAW="$OUT_DIR/raw"
mkdir -p "$RAW"
: >"$CSV"
echo "scenario,cpu_pct_avg,cpu_pct_max,rss_kb,vsz_kb,threads,fds,res,src_fps_note,detail" >>"$CSV"

export DISPLAY="${DISPLAY:-:0}"
if [[ -z "${XAUTHORITY:-}" ]]; then
  xa="$(ls /run/user/"${UID:-1000}"/.mutter-Xwaylandauth.* 2>/dev/null | head -1 || true)"
  [[ -n "$xa" ]] && export XAUTHORITY="$xa"
fi
export LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-/usr/local/lib}"

booth_was_up=0
restore_booth() {
  if [[ "$booth_was_up" -eq 1 ]]; then
    echo "Restoring booth ndi-viewer…"
    tmux kill-session -t ndi-viewer 2>/dev/null || true
    pkill -x ghostvidstream 2>/dev/null || true
    sleep 0.5
    tmux new-session -d -s ndi-viewer \
      "env DISPLAY=${DISPLAY} XAUTHORITY=${XAUTHORITY:-} LD_LIBRARY_PATH=/usr/local/lib \
       $BIN --auto --stats --fullscreen --find-ms 5000 2>&1 | tee /tmp/ndi-viewer.log"
    sleep 3
    pgrep -x ghostvidstream >/dev/null && echo "BOOTH_RESTORED" || echo "BOOTH_RESTORE_FAIL"
  fi
}
trap restore_booth EXIT

if tmux has-session -t ndi-viewer 2>/dev/null || pgrep -x ghostvidstream >/dev/null; then
  booth_was_up=1
  echo "Pausing booth viewer for profiling…"
  tmux kill-session -t ndi-viewer 2>/dev/null || true
  pkill -x ghostvidstream 2>/dev/null || true
  sleep 1
fi

sample_pid() {
  local scenario="$1"
  local pid="$2"
  local log="$3"
  local detail="${4:-}"
  local prefix="$RAW/$scenario"
  mkdir -p "$prefix"

  # Warmup
  sleep "$WARMUP"
  if ! kill -0 "$pid" 2>/dev/null; then
    echo "$scenario,FAIL,FAIL,0,0,0,0,?,?,dead_after_warmup" >>"$CSV"
    return 1
  fi

  # pidstat CPU — parse by header for %CPU column
  pidstat -u -p "$pid" 1 "$SAMPLE_SEC" >"$prefix/pidstat.txt" 2>&1 || true

  local rss vsz threads fds
  rss=$(awk '/VmRSS:/ {print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)
  vsz=$(awk '/VmSize:/ {print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)
  threads=$(awk '/Threads:/ {print $2}' "/proc/$pid/status" 2>/dev/null || echo 0)
  fds=$(ls "/proc/$pid/fd" 2>/dev/null | wc -l | tr -d ' ')

  cpu_avg=$(awk '
    BEGIN { col=0; n=0; sum=0 }
    /%CPU/ {
      for (i=1;i<=NF;i++) if ($i=="%CPU") col=i
      next
    }
    col && $0 !~ /Linux/ && $0 !~ /^$/ {
      # Sample rows: time UID PID ... %CPU ...
      # Average row starts with "Average:"
      if ($1 == "Average:" && ($(col)+0) > 0) { printf "%.1f", $(col)+0; exit }
      if ($1 != "Average:" && NF >= col) {
        v=$(col)+0
        # skip header leftovers
        if (v >= 0 && $2+0 == $2) { sum+=v; n++ }
      }
    }
    END { if (n>0) printf "%.1f", sum/n }
  ' "$prefix/pidstat.txt")
  cpu_max=$(awk '
    BEGIN { col=0; max=0 }
    /%CPU/ { for (i=1;i<=NF;i++) if ($i=="%CPU") col=i; next }
    col && $1 != "Average:" && $0 !~ /Linux/ && NF >= col {
      v=$(col)+0
      if ($2+0 == $2 && v > max) max=v
    }
    END { printf "%.1f", max }
  ' "$prefix/pidstat.txt")
  [[ -n "${cpu_avg:-}" ]] || cpu_avg="0"
  [[ -n "${cpu_max:-}" ]] || cpu_max="0"

  # Fallback: /proc jiffies over 3s if pidstat avg is 0 while process is alive
  if [[ "$cpu_avg" == "0" || "$cpu_avg" == "0.0" ]]; then
    if kill -0 "$pid" 2>/dev/null; then
      j1=$(awk '{print $14+$15+$16+$17}' "/proc/$pid/stat" 2>/dev/null || echo 0)
      sleep 3
      j2=$(awk '{print $14+$15+$16+$17}' "/proc/$pid/stat" 2>/dev/null || echo 0)
      hz=$(getconf CLK_TCK 2>/dev/null || echo 100)
      cpu_avg=$(awk -v a="$j1" -v b="$j2" -v hz="$hz" 'BEGIN{printf "%.1f", 100.0*(b-a)/(3.0*hz)}')
      if awk -v a="$cpu_avg" -v b="$cpu_max" 'BEGIN{exit !(a+0>b+0)}'; then
        cpu_max="$cpu_avg"
      fi
    fi
  fi

  # Resolution / fps from log
  local res="?"
  local fps="?"
  if grep -q 'First frame' "$log" 2>/dev/null; then
    res=$(grep 'First frame' "$log" | tail -1 | sed -n 's/.*First frame: \([0-9]*x[0-9]*\).*/\1/p')
    fps=$(grep 'First frame' "$log" | tail -1 | sed -n 's/.*src≈\([0-9.]*\) fps.*/\1/p')
  fi

  printf '%s\n' "$rss" >"$prefix/rss_kb"
  echo "$scenario cpu_avg=$cpu_avg cpu_max=$cpu_max rss=${rss}kB vsz=${vsz}kB thr=$threads fds=$fds res=$res fps=$fps $detail"
  echo "$scenario,$cpu_avg,$cpu_max,$rss,$vsz,$threads,$fds,$res,$fps,$detail" >>"$CSV"
}

run_viewer() {
  local scenario="$1"
  shift
  local log="$RAW/$scenario/viewer.log"
  mkdir -p "$RAW/$scenario"
  : >"$log"
  # Windowed — do not fullscreen (booth-friendly while profiling)
  env DISPLAY="$DISPLAY" XAUTHORITY="${XAUTHORITY:-}" LD_LIBRARY_PATH="$LD_LIBRARY_PATH" \
    "$BIN" "$@" >"$log" 2>&1 &
  local pid=$!
  echo "$pid" >"$RAW/$scenario/pid"
  # Optional key sequence after first frame
  local keys="${PROFILE_KEYS:-}"
  if [[ -n "$keys" ]] && command -v xdotool >/dev/null 2>&1; then
    (
      for _ in $(seq 1 40); do
        grep -q 'First frame' "$log" 2>/dev/null && break
        sleep 0.25
      done
      sleep 1
      wid="$(xdotool search --pid "$pid" 2>/dev/null | tail -1 || true)"
      if [[ -n "$wid" ]]; then
        xdotool windowactivate --sync "$wid" || true
        # shellcheck disable=SC2086
        for k in $keys; do
          xdotool key --window "$wid" "$k" || true
          sleep 0.15
        done
      fi
    ) &
  fi
  sample_pid "$scenario" "$pid" "$log" "$*"
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true
  sleep 0.5
}

echo "=== resource profile $(date -Is) ip=$IP ==="

# 1) No stream — auto-search for unused TEST-NET IP (never connects)
PROFILE_KEYS= run_viewer "no_stream" --ip 203.0.113.9 --auto --find-ms 2000 --rescan-ms 1500

# 2) Low bandwidth
PROFILE_KEYS= run_viewer "bw_lowest" --ip "$IP" --auto --bandwidth lowest --find-ms 4000

# 3) Highest bandwidth (max quality)
PROFILE_KEYS= run_viewer "bw_highest" --ip "$IP" --auto --bandwidth highest --find-ms 4000

# 4) Highest + stats HUD on (--stats)
PROFILE_KEYS= run_viewer "hud_on" --ip "$IP" --auto --bandwidth highest --stats --find-ms 4000

# 5) Highest, no --stats (HUD off)
PROFILE_KEYS= run_viewer "hud_off" --ip "$IP" --auto --bandwidth highest --find-ms 4000

# 6) Controls overlay open (key c)
PROFILE_KEYS="c" run_viewer "controls_on" --ip "$IP" --auto --bandwidth highest --find-ms 4000

# 7) Paused
PROFILE_KEYS="space" run_viewer "paused" --ip "$IP" --auto --bandwidth highest --stats --find-ms 4000

echo "=== CSV ==="
cat "$CSV"
echo "Wrote $CSV"
