#!/usr/bin/env python3
"""ColecoVision TMS9918 sprite top-line regression.

Runs multi_sprites.cv with the ColecoVision BIOS and verifies that sprites with
encoded Y=$ff display their first pattern row on active scanline 0 instead of
being parsed one line late and cropped.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from pathlib import Path

BASELINE_SHA256 = "d8313b14361a088d2eee5e101951f66ca9091e92b7c995de12492c80867c5850"


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


def first_row_non_black_pixels(path: Path) -> int:
    data = path.read_bytes()
    token, pos = _read_ppm_token(data, 0)
    if token != b"P6":
        raise ValueError(f"{path}: expected binary PPM/P6, got {token!r}")
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
    row = data[pos:pos + width * 3]
    if len(row) != width * 3:
        raise ValueError(f"{path}: truncated first pixel row")
    return sum(1 for i in range(0, len(row), 3) if row[i:i + 3] != b"\x00\x00\x00")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--out-dir", default=Path("test-results/coleco_multi_sprites"), type=Path)
    parser.add_argument("--frames", default=120, type=int)
    parser.add_argument("--baseline-sha256", default=BASELINE_SHA256)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    screenshot = args.out_dir / "multi_sprites.ppm"
    if screenshot.exists():
        screenshot.unlink()

    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)

    cmd = [
        str(binary),
        "--console", "coleco",
        "--coleco-bios", str(args.bios),
        "--frames", str(args.frames),
        "--screenshot", str(screenshot),
        str(args.rom),
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        print(proc.stdout, end="")
        print(proc.stderr, end="")
        return proc.returncode
    if not screenshot.exists():
        print(f"missing screenshot: {screenshot}")
        return 1

    actual = sha256_file(screenshot)
    first_row_pixels = first_row_non_black_pixels(screenshot)
    failures: list[str] = []
    if actual.lower() != args.baseline_sha256.lower():
        failures.append(f"screenshot SHA-256 mismatch: got {actual}, expected {args.baseline_sha256}")
    if first_row_pixels <= 0:
        failures.append("active scanline 0 has no sprite pixels; top sprite row is still cropped")

    ok = not failures
    if not args.quiet or not ok:
        print("ColecoVision multi_sprites regression")
        print(f"rom={args.rom.name} bios={args.bios.name} frames={args.frames}")
        print(f"screenshot={screenshot}")
        print(f"sha256={actual}")
        print(f"first_row_non_black_pixels={first_row_pixels}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
