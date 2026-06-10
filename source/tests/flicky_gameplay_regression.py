#!/usr/bin/env python3
"""Flicky System 1 collision/protection regression.

The test inserts one credit, starts one-player mode, then waits into the first
round with no player input.  On broken System 1 collision timing the player is
killed almost immediately and the captured frame is the "GAME OVER" state.  The
fixed MAME-style collision path keeps Flicky alive at this point.

If a MAME video is supplied, the script also extracts a reference frame for the
operator's archive.  The MAME capture supplied for this test is 512x224 internal
framebuffer video while MultiRex outputs 256x224, so this script does not require
raw pixel identity against the lossy MKV; the deterministic emulator hash remains
the pass/fail oracle.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile
from typing import Optional

BASELINE_FRAMES = 1440
BASELINE_SHA256 = "0de6fa69c96d2b2bfb5fa073656cf27c0cf7081f37a60fc8dea7b8557a0604bb"


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


def count_red_game_over_like_pixels(width: int, height: int, pixels: bytes) -> int:
    # The broken frame has a red GAME OVER banner near the center of the playfield.
    # Count saturated red pixels in that region as an additional diagnostic beyond
    # the exact hash.  The fixed baseline is comfortably below this threshold.
    x0, x1 = width // 3, min(width, 2 * width // 3)
    y0, y1 = height // 2 - 32, min(height, height // 2 + 32)
    count = 0
    for y in range(max(0, y0), y1):
        row = y * width * 3
        for x in range(max(0, x0), x1):
            off = row + x * 3
            r, g, b = pixels[off], pixels[off + 1], pixels[off + 2]
            if r > 180 and g < 80 and b < 80:
                count += 1
    return count


def extract_mame_frame(video: Path, frame_number_1_based: int, out_path: Path) -> None:
    n = max(0, frame_number_1_based - 1)
    vf = f"select=eq(n\\,{n})"
    cmd = ["ffmpeg", "-y", "-v", "error", "-i", str(video), "-vf", vf, "-frames:v", "1", str(out_path)]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr or proc.stdout or f"ffmpeg failed with exit {proc.returncode}")
    if not out_path.exists() or out_path.stat().st_size == 0:
        raise RuntimeError(f"ffmpeg did not extract MAME frame {frame_number_1_based} from {video}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out-dir", default=Path("test-results/flicky_gameplay"), type=Path)
    parser.add_argument("--frames", default=BASELINE_FRAMES, type=int)
    parser.add_argument("--baseline-sha256", default=BASELINE_SHA256)
    parser.add_argument("--mame-video", default=None, type=Path)
    parser.add_argument("--mame-frame", default=BASELINE_FRAMES, type=int)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    screenshot = args.out_dir / "flicky_gameplay.ppm"
    input_script = args.out_dir / "flicky_start.input"
    input_script.write_text("600f:COIN1@12\n624f:START1@12\n", encoding="utf-8")
    if screenshot.exists():
        screenshot.unlink()

    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)
    cmd = [
        str(binary),
        "--console", "system1",
        "--frames", str(args.frames),
        "--input-playback", str(input_script),
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
    red_game_over_pixels = count_red_game_over_like_pixels(width, height, pixels)
    mame_frame_path: Optional[Path] = None
    failures: list[str] = []

    if actual.lower() != args.baseline_sha256.lower():
        failures.append(f"screenshot SHA-256 mismatch: got {actual}, expected {args.baseline_sha256}")
    if width != 256 or height != 224:
        failures.append(f"unexpected Flicky visible size: {width}x{height}, expected 256x224")
    if red_game_over_pixels > 40:
        failures.append(f"GAME OVER-like red pixels in playfield: {red_game_over_pixels}, expected <= 40")

    if args.mame_video is not None:
        if not args.mame_video.exists():
            failures.append(f"MAME reference video not found: {args.mame_video}")
        else:
            try:
                mame_frame_path = args.out_dir / f"mame_frame_{args.mame_frame:06d}.png"
                extract_mame_frame(args.mame_video, args.mame_frame, mame_frame_path)
            except Exception as exc:
                failures.append(f"MAME reference extraction failed: {exc}")

    ok = not failures
    if not args.quiet or not ok:
        print("Flicky gameplay collision/protection regression")
        print(f"rom={args.rom}")
        print(f"frames={args.frames}")
        print(f"screenshot={screenshot}")
        print(f"sha256={actual}")
        print(f"red_game_over_pixels={red_game_over_pixels}")
        if args.mame_video is not None:
            print(f"mame_video={args.mame_video}")
            print(f"mame_frame={args.mame_frame}")
            print(f"mame_extracted_frame={mame_frame_path}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
