# Stress tests for libghost_ndihx / ghostvidstream

Headless + light UI stresses for the local Linux booth host.

## Build

```bash
make stress          # stress_ghost_ndihx
make stress-asan     # stress_ghost_ndihx-asan (AddressSanitizer)
```

## Run (on 172.16.1.144)

```bash
export NDI_STRESS_IP=172.16.1.189
# Optional: shorter soak for a quick pass
# export NDI_STRESS_SOAK_SEC=120
./tests/stress/run_all.sh
```

Logs land in `/tmp/ndi-stress/` (`summary.txt`, per-suite `*.out`).

## Suites

| Suite | What |
| --- | --- |
| soak | Long receive; FPS / drops / RSS growth |
| reconnect | Rapid disconnect + `connect_auto` |
| bw | Bandwidth highest↔lowest while receiving |
| discover | Repeated `ghost_ndihx_discover` |
| list_churn | CLI `--list` rounds |
| wrongip | Connect to TEST-NET IP must fail cleanly |
| multi | Two simultaneous sessions |
| ptz | Move/stop spam if `MEDIA_CAP_PTZ` |
| cpu_load | Soak under `stress-ng --cpu` |
| asan | Short soak under ASan |
| valgrind | Short reconnect under Valgrind |
| ui_thrash | Windowed viewer + `xdotool` key spam (`c`/`i`/bw/fps) |

`run_all.sh` pauses tmux `ndi-viewer` only for UI thrash, then restores `--auto --stats --fullscreen`.

## Env knobs

`NDI_STRESS_IP`, `NDI_STRESS_SOAK_SEC`, `NDI_STRESS_RECONNECT_N`, `NDI_STRESS_BW_FLAPS`,
`NDI_STRESS_DISCOVER_N`, `NDI_STRESS_PTZ_CYCLES`, `NDI_STRESS_MULTI_SEC`,
`NDI_STRESS_UI_SEC`, `NDI_STRESS_ASAN_SEC`, `NDI_STRESS_CPU_SEC`, `NDI_STRESS_LOG_DIR`.
