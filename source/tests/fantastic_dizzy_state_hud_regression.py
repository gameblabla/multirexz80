#!/usr/bin/env python3
"""Fantastic Dizzy SMS save-state top HUD stability regression.

Loads a PNG/.sgxst state, holds RIGHT, captures every frame, and verifies that
Fantastic Dizzy's top HUD frame remains present.  The entire 31-pixel region is
not expected to be byte-identical while walking: the game can expose scrolling
playfield pixels through transparent parts of the status band and can update
status text.  The regression instead checks stable HUD scanlines plus simple
color-coverage guards that catch the intermittent top-latch drop.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise AssertionError(f"{path}: expected binary PPM P6")
    idx = 3
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while idx < len(data) and data[idx] in b" \t\r\n":
            idx += 1
        if idx < len(data) and data[idx] == ord('#'):
            while idx < len(data) and data[idx] not in b"\r\n":
                idx += 1
            continue
        start = idx
        while idx < len(data) and data[idx] not in b" \t\r\n":
            idx += 1
        tokens.append(data[start:idx])
    width = int(tokens[0])
    height = int(tokens[1])
    maxval = int(tokens[2])
    if maxval != 255:
        raise AssertionError(f"{path}: expected maxval 255, got {maxval}")
    while idx < len(data) and data[idx] in b" \t\r\n":
        idx += 1
    pixels = data[idx:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise AssertionError(f"{path}: expected {expected} RGB bytes, got {len(pixels)}")
    return width, height, pixels


def crop_bytes(path: Path, y0: int, y1: int) -> bytes:
    width, height, pixels = read_ppm(path)
    if y0 < 0 or y1 <= y0 or y1 > height:
        raise AssertionError(f"invalid crop {y0}:{y1} for {path}: image height {height}")
    return pixels[y0 * width * 3 : y1 * width * 3]


def color_coverage(path: Path, height: int) -> tuple[int, int, int, int]:
    width, image_height, pixels = read_ppm(path)
    if height <= 0 or height > image_height:
        raise AssertionError(f"invalid HUD height {height} for {path}: image height {image_height}")
    hud = pixels[: width * height * 3]
    orange = green = dark = cyan = 0
    for i in range(0, len(hud), 3):
        r, g, b = hud[i], hud[i + 1], hud[i + 2]
        if r > 150 and 35 < g < 190 and b < 90:
            orange += 1
        if g > 100 and r < 130 and b < 130:
            green += 1
        if r + g + b < 45:
            dark += 1
        if b > 130 and g > 80 and r < 80:
            cyan += 1
    return orange, green, dark, cyan


def run_checked(cmd: list[str], cwd: Path, timeout: float) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd), timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError("command failed with status %d: %s" % (proc.returncode, " ".join(cmd)))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Verify Fantastic Dizzy save-state top HUD while holding RIGHT.")
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--load-state", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, required=True)
    ap.add_argument("--frames", type=int, default=600)
    ap.add_argument("--hud-height", type=int, default=31)
    ap.add_argument("--stable-top-lines", type=int, default=8)
    ap.add_argument("--separator-y0", type=int, default=31)
    ap.add_argument("--separator-y1", type=int, default=33)
    ap.add_argument("--start-frame", type=int, default=2)
    ap.add_argument("--console", default="sms2")
    ap.add_argument("--region", default="pal")
    ap.add_argument("--timeout", type=float, default=240.0)
    args = ap.parse_args(argv)

    binary = args.binary.resolve()
    rom = args.rom.resolve()
    load_state = args.load_state.resolve()
    out_dir = args.out_dir.resolve()

    if not binary.exists():
        raise SystemExit(f"headless binary not found: {binary}")
    if not rom.exists():
        raise SystemExit(f"ROM not found: {rom}")
    if not load_state.exists():
        raise SystemExit(f"state not found: {load_state}")
    if args.frames <= 0:
        raise SystemExit("--frames must be positive")

    out_dir.mkdir(parents=True, exist_ok=True)
    input_script = out_dir / "hold_right.input"
    input_script.write_text(f"0f:+R\n{args.frames}f:-R\n", encoding="utf-8")
    prefix = out_dir / "fantastic_dizzy_state_right"

    cmd = [
        str(binary),
        "--console", args.console,
        "--region", args.region,
        "--load-state", str(load_state),
        "--input-playback", str(input_script),
        "--frames", str(args.frames),
        "--screenshot-prefix", str(prefix),
        "--screenshot-every", "1",
        "--quiet",
        str(rom),
    ]
    run_checked(cmd, binary.parent, args.timeout)

    baseline_frame = max(1, min(args.start_frame, args.frames))
    baseline_path = Path(f"{prefix}_{baseline_frame:06d}.ppm")
    baseline_static = hashlib.sha256(crop_bytes(baseline_path, 0, args.stable_top_lines)).hexdigest()
    baseline_separator = hashlib.sha256(crop_bytes(baseline_path, args.separator_y0, args.separator_y1)).hexdigest()
    baseline_orange, baseline_green, baseline_dark, _ = color_coverage(baseline_path, args.hud_height)

    min_orange = max(1, int(baseline_orange * 0.75))
    min_green = max(1, int(baseline_green * 0.75))
    min_dark = max(1, int(baseline_dark * 0.70))

    failures: list[str] = []
    for frame in range(baseline_frame, args.frames + 1):
        p = Path(f"{prefix}_{frame:06d}.ppm")
        if not p.exists():
            failures.append(f"missing screenshot {p.name}")
            continue
        stable = hashlib.sha256(crop_bytes(p, 0, args.stable_top_lines)).hexdigest()
        if stable != baseline_static:
            failures.append(f"frame {frame}: top {args.stable_top_lines}px changed ({stable} != {baseline_static})")
            continue
        separator = hashlib.sha256(crop_bytes(p, args.separator_y0, args.separator_y1)).hexdigest()
        if separator != baseline_separator:
            failures.append(f"frame {frame}: separator lines {args.separator_y0}:{args.separator_y1} changed ({separator} != {baseline_separator})")
            continue
        orange, green, dark, cyan = color_coverage(p, args.hud_height)
        if orange < min_orange or green < min_green or dark < min_dark:
            failures.append(
                f"frame {frame}: HUD coverage too low orange={orange}/{min_orange} green={green}/{min_green} dark={dark}/{min_dark} cyan={cyan}"
            )

    if failures:
        print("Fantastic Dizzy save-state HUD instability detected")
        for f in failures[:20]:
            print("  -", f)
        if len(failures) > 20:
            print(f"  ... {len(failures) - 20} more")
        return 1

    print(
        "Fantastic Dizzy save-state HUD stable for "
        f"{args.frames} frames; static top SHA {baseline_static}; "
        f"separator SHA {baseline_separator}; "
        f"coverage minima orange={min_orange} green={min_green} dark={min_dark}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
