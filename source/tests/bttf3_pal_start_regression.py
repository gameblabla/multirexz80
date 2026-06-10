#!/usr/bin/env python3
"""Back to the Future Part III PAL SMS2 boot/start regression.

This game intentionally probes the SMS VDP vertical status flag timing during
boot.  If the VBlank flag can be re-latched after a status-port read in the
same VBlank event, the game trips its failure path and never reaches the title
sequence/gameplay.  The test verifies both the database-driven PAL SMS2
selection and the explicit command-line PAL SMS2 path, then presses Button 2 on
the title screen and checks that gameplay is reached.
"""
from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path
import subprocess

BASELINE_FRAMES = 3000
BASELINE_SHA256 = "0e51587039f69338c8b617fa82a916715c78c41427a71a595e47d16fd38ef419"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _read_ppm_token(data: bytes, pos: int) -> tuple[bytes, int]:
    n = len(data)
    while pos < n and data[pos] in b" \t\r\n":
        pos += 1
    if pos < n and data[pos] == ord("#"):
        while pos < n and data[pos] not in b"\r\n":
            pos += 1
        return _read_ppm_token(data, pos)
    start = pos
    while pos < n and data[pos] not in b" \t\r\n":
        pos += 1
    return data[start:pos], pos


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    token, pos = _read_ppm_token(data, 0)
    if token != b"P6":
        raise ValueError(f"{path}: expected PPM/P6, got {token!r}")
    token, pos = _read_ppm_token(data, pos)
    width = int(token)
    token, pos = _read_ppm_token(data, pos)
    height = int(token)
    token, pos = _read_ppm_token(data, pos)
    maxval = int(token)
    if width <= 0 or height <= 0 or maxval != 255:
        raise ValueError(f"{path}: unsupported PPM geometry {width}x{height} max={maxval}")
    while pos < len(data) and data[pos] in b" \t\r\n":
        pos += 1
    pixels = data[pos:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: expected {width * height * 3} RGB bytes, got {len(pixels)}")
    return width, height, pixels


def count_sky_and_gameplay_pixels(width: int, height: int, pixels: bytes) -> tuple[int, int]:
    sky = 0
    hud = 0
    for y in range(0, min(height, 192)):
        row = y * width * 3
        for x in range(width):
            off = row + x * 3
            r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
            if b > 180 and g > 120 and r < 40:
                sky += 1
            if y >= 160 and r > 180 and g > 180 and b > 140:
                hud += 1
    return sky, hud


def run_cmd(cmd: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)


def parse_metadata(output: str) -> tuple[str | None, str | None, str | None]:
    m_crc = re.search(r"\bcrc=([0-9A-Fa-f]{8})\b", output)
    m_console = re.search(r"\bconsole=(\d+)\b", output)
    m_display = re.search(r"\bdisplay=([A-Z]+)\b", output)
    return (
        m_crc.group(1).upper() if m_crc else None,
        m_console.group(1) if m_console else None,
        m_display.group(1) if m_display else None,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", required=True, type=Path)
    ap.add_argument("--rom", required=True, type=Path)
    ap.add_argument("--out-dir", default=Path("test-results/bttf3_pal_start"), type=Path)
    ap.add_argument("--frames", default=BASELINE_FRAMES, type=int)
    ap.add_argument("--baseline-sha256", default=BASELINE_SHA256)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)
    screenshot = args.out_dir / "bttf3_pal_gameplay.ppm"
    input_script = args.out_dir / "bttf3_start.input"
    input_script.write_text("1800f:B@30\n", encoding="utf-8")
    if screenshot.exists():
        screenshot.unlink()

    failures: list[str] = []

    auto_probe = run_cmd([str(binary), "--frames", "1", str(args.rom)])
    auto_text = auto_probe.stdout + auto_probe.stderr
    auto_crc, auto_console, auto_display = parse_metadata(auto_text)
    if auto_probe.returncode != 0:
        failures.append(f"auto metadata probe failed with exit {auto_probe.returncode}: {auto_text.strip()}")
    if auto_crc != "2D48C1D3" or auto_console != "33" or auto_display != "PAL":
        failures.append(f"auto metadata mismatch: crc={auto_crc} console={auto_console} display={auto_display}, expected 2D48C1D3/33/PAL")

    cmd = [
        str(binary),
        "--console", "sms2",
        "--region", "pal",
        "--frames", str(args.frames),
        "--input-playback", str(input_script),
        "--screenshot", str(screenshot),
        str(args.rom),
    ]
    proc = run_cmd(cmd)
    text = proc.stdout + proc.stderr
    run_crc, run_console, run_display = parse_metadata(text)
    if proc.returncode != 0:
        failures.append(f"forced PAL SMS2 gameplay run failed with exit {proc.returncode}: {text.strip()}")
    if run_crc != "2D48C1D3" or run_console != "33" or run_display != "PAL":
        failures.append(f"forced-run metadata mismatch: crc={run_crc} console={run_console} display={run_display}, expected 2D48C1D3/33/PAL")
    if not screenshot.exists():
        failures.append(f"missing screenshot: {screenshot}")
        actual = "<missing>"
        sky = hud = 0
    else:
        actual = sha256_file(screenshot)
        try:
            width, height, pixels = read_ppm(screenshot)
            sky, hud = count_sky_and_gameplay_pixels(width, height, pixels)
            if width != 256 or height != 192:
                failures.append(f"unexpected SMS visible size: {width}x{height}, expected 256x192")
            if sky < 12000 or hud < 1000:
                failures.append(f"gameplay-pixel check failed: sky={sky}, hud={hud}")
        except Exception as exc:
            sky = hud = 0
            failures.append(f"screenshot parse/check failed: {exc}")
        if actual.lower() != args.baseline_sha256.lower():
            failures.append(f"screenshot SHA-256 mismatch: got {actual}, expected {args.baseline_sha256}")

    ok = not failures
    if not args.quiet or not ok:
        print("Back to the Future Part III PAL SMS2 start regression")
        print(f"rom={args.rom}")
        print(f"auto_crc={auto_crc} auto_console={auto_console} auto_display={auto_display}")
        print(f"run_crc={run_crc} run_console={run_console} run_display={run_display}")
        print(f"frames={args.frames}")
        print(f"screenshot={screenshot}")
        print(f"sha256={actual}")
        print(f"sky_pixels={sky}")
        print(f"hud_pixels={hud}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
