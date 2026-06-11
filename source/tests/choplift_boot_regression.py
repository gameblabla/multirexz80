#!/usr/bin/env python3
"""Choplifter parent-set boot/sanity regression.

The parent MAME set (choplift.zip) uses the protected Sega System 2 ROM names
and must not silently fall through to the generic SMS/black-screen path.  This
smoke test locks the early IC CHECK frame and the later title-screen frame.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
from typing import Tuple

EXPECTED_360_SHA256 = "76ce913e1176dcb97dbeee1a246bdf92a1da9f051c6111b551942772138a62b4"
EXPECTED_3600_SHA256 = "cab50edda78c539cc50229db9f0208c19e9a86be101cd14aafa30e88eb610b7b"
EXPECTED_W = 256
EXPECTED_H = 224


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_ppm(path: Path) -> Tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise AssertionError(f"{path}: expected binary PPM/P6")
    pos = 3
    tokens: list[bytes] = []
    while len(tokens) < 3:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            while data[pos:pos + 1] != b"\n":
                pos += 1
            continue
        start = pos
        while data[pos:pos + 1] and not data[pos:pos + 1].isspace():
            pos += 1
        tokens.append(data[start:pos])
    while data[pos:pos + 1].isspace():
        pos += 1
    w, h, maxval = map(int, tokens)
    if maxval != 255:
        raise AssertionError(f"{path}: expected maxval 255, got {maxval}")
    pixels = data[pos:]
    if len(pixels) != w * h * 3:
        raise AssertionError(f"{path}: malformed pixel data length {len(pixels)} for {w}x{h}")
    return w, h, pixels


def visible_nonblack_pixels(ppm: Path) -> int:
    w, h, pixels = read_ppm(ppm)
    if (w, h) != (EXPECTED_W, EXPECTED_H):
        raise AssertionError(f"{ppm}: expected {EXPECTED_W}x{EXPECTED_H}, got {w}x{h}")
    count = 0
    for i in range(0, len(pixels), 3):
        if pixels[i] or pixels[i + 1] or pixels[i + 2]:
            count += 1
    return count


def run_frame(binary: Path, rom: Path, frames: int, out: Path, quiet: bool) -> None:
    cmd = [str(binary), "--console", "system1", "--frames", str(frames), "--screenshot", str(out)]
    if quiet:
        cmd.append("--quiet")
    cmd.append(str(rom))
    subprocess.run(cmd, check=True)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Choplifter parent boot visual regression.")
    ap.add_argument("--binary", type=Path, default=Path("./multirexz80_headless"))
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, default=Path("test-results/choplift_boot"))
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args(argv)

    if not args.binary.exists():
        raise SystemExit(f"headless binary not found: {args.binary}")
    args.binary = args.binary.resolve()
    if not args.rom.exists():
        raise SystemExit(f"ROM not found: {args.rom}")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    ic_ppm = args.out_dir / "choplift_0360_ic_check.ppm"
    title_ppm = args.out_dir / "choplift_3600_title.ppm"
    run_frame(args.binary, args.rom, 360, ic_ppm, args.quiet)
    run_frame(args.binary, args.rom, 3600, title_ppm, args.quiet)

    ic_sha = sha256_file(ic_ppm)
    title_sha = sha256_file(title_ppm)
    ic_nonblack = visible_nonblack_pixels(ic_ppm)
    title_nonblack = visible_nonblack_pixels(title_ppm)

    failures: list[str] = []
    if ic_sha != EXPECTED_360_SHA256:
        failures.append(f"IC CHECK frame SHA mismatch: got {ic_sha}, expected {EXPECTED_360_SHA256}")
    if title_sha != EXPECTED_3600_SHA256:
        failures.append(f"title frame SHA mismatch: got {title_sha}, expected {EXPECTED_3600_SHA256}")
    if ic_nonblack < 10:
        failures.append(f"IC CHECK frame still appears black: nonblack pixels={ic_nonblack}")
    if title_nonblack < 10000:
        failures.append(f"title frame appears blank: nonblack pixels={title_nonblack}")

    print("Choplifter boot regression")
    print(f"  IC CHECK frame SHA-256: {ic_sha} nonblack={ic_nonblack}")
    print(f"  title frame SHA-256:    {title_sha} nonblack={title_nonblack}")
    print(f"  outputs: {ic_ppm}, {title_ppm}")
    if failures:
        print("FAIL")
        for f in failures:
            print("  -", f)
        return 1
    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
