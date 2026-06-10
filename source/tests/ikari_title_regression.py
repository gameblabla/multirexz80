#!/usr/bin/env python3
"""Ikari Warriors title-screen renderer regression.

The title background is real background-tile pen 0x0f grey on MAME/hardware,
not a black backdrop.  This catches accidental special-casing of that pen and
also protects the MAME-style sprite shadow/title composition.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from pathlib import Path

BASELINE_SHA256 = "edd9ff6627d4f8705264ab46628c496587aa0a34fb38ba0c9bcb6ad52f3d46cf"
BASELINE_FRAMES = 593


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
    pixels = data[pos:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"{path}: expected {expected} RGB bytes, got {len(pixels)}")
    return width, height, pixels


def pixel_at(width: int, pixels: bytes, x: int, y: int) -> tuple[int, int, int]:
    off = (y * width + x) * 3
    return pixels[off], pixels[off + 1], pixels[off + 2]


def count_near_grey(pixels: bytes, target: int = 81, tolerance: int = 2) -> int:
    count = 0
    lo = target - tolerance
    hi = target + tolerance
    for i in range(0, len(pixels), 3):
        r, g, b = pixels[i], pixels[i + 1], pixels[i + 2]
        if lo <= r <= hi and lo <= g <= hi and lo <= b <= hi:
            count += 1
    return count


def count_black_border_rows(width: int, height: int, pixels: bytes) -> int:
    count = 0
    for y in list(range(min(16, height))) + list(range(max(0, height - 16), height)):
        for x in range(width):
            off = (y * width + x) * 3
            if pixels[off] == 0 and pixels[off + 1] == 0 and pixels[off + 2] == 0:
                count += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out-dir", default=Path("test-results/ikari_title"), type=Path)
    parser.add_argument("--frames", default=BASELINE_FRAMES, type=int)
    parser.add_argument("--baseline-sha256", default=BASELINE_SHA256)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    screenshot = args.out_dir / "ikari_title.ppm"
    if screenshot.exists():
        screenshot.unlink()

    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)
    cmd = [
        str(binary),
        "--console", "psychos",
        "--frames", str(args.frames),
        "--screenshot", str(screenshot),
        "--quiet",
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
    width, height, pixels = read_ppm(screenshot)
    grey_count = count_near_grey(pixels)
    black_border_count = count_black_border_rows(width, height, pixels)
    # Sample an exposed background pixel below the title logo and away from text/sprites.
    bg_sample = pixel_at(width, pixels, 20, 160) if width > 20 and height > 160 else (0, 0, 0)

    failures: list[str] = []
    if actual.lower() != args.baseline_sha256.lower():
        failures.append(f"screenshot SHA-256 mismatch: got {actual}, expected {args.baseline_sha256}")
    if grey_count < 30000:
        failures.append(f"title background is not predominantly grey; grey-like pixels={grey_count}")
    if max(bg_sample) < 60:
        failures.append(f"title background sample at (20,160) is too dark/black: {bg_sample}")
    if black_border_count < 3000:
        failures.append(f"MAME Ikari rotated top/bottom borders are not black enough: black_border_pixels={black_border_count}")

    ok = not failures
    if not args.quiet or not ok:
        print("Ikari Warriors title regression")
        print(f"rom={args.rom.name} frames={args.frames}")
        print(f"screenshot={screenshot}")
        print(f"sha256={actual}")
        print(f"grey_like_pixels={grey_count}")
        print(f"black_border_pixels={black_border_count}")
        print(f"background_sample_20_160={bg_sample}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
