#!/usr/bin/env python3
"""AIDA encode parity sweep: SRT / RTMP / RTSP vs GhostVidStream URL modules.

Credentials from env (CAM_USER/CAM_PASS) — never written to results or docs.
Runs locally against stress_url_rx (same media_core path as embeds).
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

CAM = os.environ.get("CAM_HOST", "172.16.1.189")
OUT = Path(os.environ.get("SWEEP_OUT", "/tmp/aida-url-sweep"))
OUT.mkdir(parents=True, exist_ok=True)
RESULTS = OUT / "sweep_results.jsonl"
SUMMARY = OUT / "sweep_summary.json"
STRESS = Path(os.environ.get(
    "STRESS_BIN",
    "/Volumes/My Shared Files/projects/ndi-for-linux/stress_url_rx",
))

CAM_USER = os.environ.get("CAM_USER", "admin")
CAM_PASS = os.environ.get("CAM_PASS") or os.environ.get("AIDA_PASS") or ""
if not CAM_PASS:
    raise SystemExit("CAM_PASS (or AIDA_PASS) required in env")

PROTOS = ("srt", "rtmp", "rtsp")

BASELINE_MAIN = {
    "enable": 1,
    "format": "1920X1080P@30Hz",
    "mode": "h264",
    "profile": "MP",
    "bitrate": 4096,
    "rcmode": "cbr",
    "interval": 30,
}
BASELINE_SUB = {
    "enable": 1,
    "format": "1280X720P@30Hz",
    "mode": "h264",
    "profile": "MP",
    "bitrate": 2048,
    "rcmode": "cbr",
    "interval": 30,
}


def cam_api(func: str, payload: dict, key: int | None = None) -> dict:
    url = f"http://{CAM}/cgi-bin/web.fcgi?func={func}"
    body = dict(payload)
    body["key"] = int(key or 0)
    req = urllib.request.Request(
        url,
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json", "Accept": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=20) as r:
        return json.loads(r.read().decode())


def cam_login() -> int:
    j = cam_api("get", {"system": {"login": f"{CAM_USER}:{CAM_PASS}"}})
    login = j.get("system", {}).get("login") if isinstance(j.get("system"), dict) else None
    if not j.get("status") or login in (False, None, ""):
        raise RuntimeError("camera login failed")
    return int(login)


def get_venc(key: int) -> dict:
    j = cam_api("get", {"venc": {"main": True, "sub": True}}, key)
    if not j.get("status"):
        raise RuntimeError("get venc failed")
    return j["venc"]


def set_venc(key: int, main: dict | None = None, sub: dict | None = None) -> dict:
    payload = {"venc": {}}
    if main is not None:
        payload["venc"]["main"] = main
    if sub is not None:
        payload["venc"]["sub"] = sub
    return cam_api("set", payload, key)


def slim_main(m: dict) -> dict:
    out = {
        "enable": int(m.get("enable", 1)),
        "format": m["format"],
        "mode": m["mode"],
        "bitrate": int(m["bitrate"]),
        "rcmode": m["rcmode"],
        "interval": int(m["interval"]),
    }
    if m["mode"] == "h264":
        out["profile"] = m.get("profile", "MP")
    return out


def parse_wh(fmt: str) -> tuple[int, int]:
    m = re.match(r"(\d+)X(\d+)", fmt.upper())
    if not m:
        return (0, 0)
    return int(m.group(1)), int(m.group(2))


def probe(proto: str, expect_w: int, expect_h: int, soak: int = 3) -> dict:
    cmd = [
        str(STRESS),
        "--protocol",
        proto,
        "--ip",
        CAM,
        "--soak",
        str(soak),
        "--reconnect",
        "0",
    ]
    text = ""
    cp = None
    for attempt in range(2):
        try:
            cp = subprocess.run(cmd, capture_output=True, text=True, timeout=soak + 30)
            text = (cp.stdout or "") + (cp.stderr or "")
        except subprocess.TimeoutExpired:
            text = "PROBE_TIMEOUT"
            cp = None
        if "PASS first_frame" in text and "RESULT PASS" in text:
            break
        time.sleep(3.0)  # encoder / demux settle retry (esp. after H.265)
    first = None
    m = re.search(r"PASS first_frame (\d+)x(\d+)", text)
    if m:
        first = {"w": int(m.group(1)), "h": int(m.group(2))}
    ok = bool(first) and "RESULT PASS" in text
    if first and expect_w and expect_h:
        ok = ok and first["w"] == expect_w and first["h"] == expect_h
    return {
        "ok": ok,
        "first": first,
        "exit": None if cp is None else cp.returncode,
        "snippet": [ln for ln in text.splitlines() if ln.startswith(("CONNECTED", "PASS", "FAIL", "RESULT"))][:8],
    }


def log_row(row: dict) -> None:
    with RESULTS.open("a") as f:
        f.write(json.dumps(row) + "\n")
    status = "PASS" if row.get("ok") else "FAIL"
    print(f"  {status} {row.get('proto')} {row.get('label')}", flush=True)


def apply_and_wait(key: int, main: dict, settle: float | None = None) -> dict:
    # Codec / format switches (esp. H.264↔H.265) need longer encoder restart.
    if settle is None:
        settle = 8.0 if main.get("mode") == "h265" else 5.0
    set_venc(key, main=slim_main(main), sub=slim_main(BASELINE_SUB))
    time.sleep(settle)
    return get_venc(key)["main"]


def matrix_cells() -> list[dict]:
    cells: list[dict] = []
    # Encode modes
    for mode in ("h264", "h265"):
        cells.append({
            "label": f"mode_{mode}",
            "main": {**BASELINE_MAIN, "mode": mode, **({"profile": "MP"} if mode == "h264" else {})},
        })
        if mode == "h265":
            cells[-1]["main"].pop("profile", None)
    # H.264 profiles
    for prof in ("MP", "HP"):
        cells.append({
            "label": f"profile_{prof}",
            "main": {**BASELINE_MAIN, "profile": prof},
        })
    # Formats
    formats = [
        "1920X1080P@60Hz",
        "1920X1080P@59.94Hz",
        "1920X1080P@50Hz",
        "1920X1080P@30Hz",
        "1920X1080P@29.97Hz",
        "1920X1080P@25Hz",
        "1280X720P@60Hz",
        "1280X720P@30Hz",
        "1280X720P@25Hz",
    ]
    for fmt in formats:
        cells.append({
            "label": f"fmt_{fmt}",
            "main": {**BASELINE_MAIN, "format": fmt, "bitrate": 8192 if "@60" in fmt or "@59" in fmt or "@50" in fmt else 4096},
        })
    # Bitrate
    for br in (1024, 2048, 4096, 8192, 16384):
        cells.append({
            "label": f"br_{br}",
            "main": {**BASELINE_MAIN, "bitrate": br},
        })
    # RC
    for rc in ("cbr", "vbr"):
        cells.append({
            "label": f"rc_{rc}",
            "main": {**BASELINE_MAIN, "rcmode": rc},
        })
    # GOP
    for gop in (3, 15, 30, 60, 120):
        cells.append({
            "label": f"gop_{gop}",
            "main": {**BASELINE_MAIN, "interval": gop},
        })
    # Intersections
    cells.extend([
        {"label": "h265_1080p60", "main": {"enable": 1, "format": "1920X1080P@60Hz", "mode": "h265", "bitrate": 8192, "rcmode": "cbr", "interval": 60}},
        {"label": "h265_vbr_1080p30", "main": {"enable": 1, "format": "1920X1080P@30Hz", "mode": "h265", "bitrate": 4096, "rcmode": "vbr", "interval": 30}},
        {"label": "h264_hp_1080p60", "main": {"enable": 1, "format": "1920X1080P@60Hz", "mode": "h264", "profile": "HP", "bitrate": 8192, "rcmode": "cbr", "interval": 60}},
        {"label": "h264_min_br_60", "main": {"enable": 1, "format": "1920X1080P@60Hz", "mode": "h264", "profile": "MP", "bitrate": 1024, "rcmode": "cbr", "interval": 30}},
    ])
    return cells


def stress_subset() -> list[dict]:
    return [
        {"label": "stress_baseline", "main": dict(BASELINE_MAIN)},
        {"label": "stress_1080p60", "main": {**BASELINE_MAIN, "format": "1920X1080P@60Hz", "bitrate": 8192}},
        {"label": "stress_h265", "main": {"enable": 1, "format": "1920X1080P@30Hz", "mode": "h265", "bitrate": 4096, "rcmode": "cbr", "interval": 30}},
        {"label": "stress_720p60", "main": {**BASELINE_MAIN, "format": "1280X720P@60Hz", "bitrate": 4096}},
    ]


def run_matrix(key: int) -> list[dict]:
    rows = []
    if RESULTS.exists():
        RESULTS.unlink()
    for cell in matrix_cells():
        echoed = apply_and_wait(key, cell["main"])
        ew, eh = parse_wh(echoed.get("format", cell["main"]["format"]))
        for proto in PROTOS:
            pr = probe(proto, ew, eh, soak=3)
            row = {
                "ts": datetime.now(timezone.utc).isoformat(),
                "kind": "matrix",
                "label": cell["label"],
                "proto": proto,
                "requested": slim_main(cell["main"]),
                "echoed_format": echoed.get("format"),
                "echoed_mode": echoed.get("mode"),
                **pr,
            }
            log_row(row)
            rows.append(row)
    return rows


def run_stress(key: int) -> list[dict]:
    rows = []
    for cell in stress_subset():
        echoed = apply_and_wait(key, cell["main"], settle=5.0)
        ew, eh = parse_wh(echoed.get("format", cell["main"]["format"]))
        for proto in PROTOS:
            # longer soak + reconnects
            cmd = [str(STRESS), "--protocol", proto, "--ip", CAM, "--soak", "20", "--reconnect", "2"]
            try:
                cp = subprocess.run(cmd, capture_output=True, text=True, timeout=90)
                text = (cp.stdout or "") + (cp.stderr or "")
            except subprocess.TimeoutExpired:
                text = "STRESS_TIMEOUT"
            ok = "RESULT PASS" in text and "PASS first_frame" in text
            m = re.search(r"PASS first_frame (\d+)x(\d+)", text)
            first = {"w": int(m.group(1)), "h": int(m.group(2))} if m else None
            if first and ew and eh:
                ok = ok and first["w"] == ew and first["h"] == eh
            row = {
                "ts": datetime.now(timezone.utc).isoformat(),
                "kind": "stress",
                "label": cell["label"],
                "proto": proto,
                "ok": ok,
                "first": first,
                "snippet": [ln for ln in text.splitlines() if ln.startswith(("CONNECTED", "PASS", "FAIL", "RESULT"))][:10],
            }
            log_row(row)
            rows.append(row)
    return rows


def main() -> None:
    if not STRESS.is_file():
        raise SystemExit(f"missing stress binary: {STRESS}")
    key = cam_login()
    print(f"logged in cam={CAM} stress={STRESS}", flush=True)
    before = get_venc(key)
    (OUT / "baseline_before.json").write_text(json.dumps(before, indent=2))

    matrix = run_matrix(key)
    stress = run_stress(key)

    # restore
    set_venc(key, main=slim_main(BASELINE_MAIN), sub=slim_main(BASELINE_SUB))
    time.sleep(3)
    after = get_venc(key)
    (OUT / "baseline_after.json").write_text(json.dumps(after, indent=2))

    def tally(rows: list[dict]) -> dict:
        by = {}
        for r in rows:
            p = r["proto"]
            by.setdefault(p, {"pass": 0, "fail": 0})
            if r.get("ok"):
                by[p]["pass"] += 1
            else:
                by[p]["fail"] += 1
        return by

    summary = {
        "when": datetime.now(timezone.utc).isoformat(),
        "cam": CAM,
        "matrix": tally(matrix),
        "stress": tally(stress),
        "matrix_total": len(matrix),
        "stress_total": len(stress),
        "ndi_only_note": "Viewer bandwidth / NDI discovery knobs are NDI-only — not applicable to SRT/RTMP/RTSP.",
        "baseline_restored": after.get("main", {}).get("format") == BASELINE_MAIN["format"],
    }
    SUMMARY.write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
