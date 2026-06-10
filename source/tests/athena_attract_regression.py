#!/usr/bin/env python3
"""Athena attract-mode MAME renderer regression.

This protects the TNK III/Athena side HUD composition.  MAME draws Athena
through a 36x28 visible area; neither the playfield background nor off-playfield
sprites may bleed behind the left/right HUD strips during attract gameplay.

When --mame-video is supplied, the test also extracts the requested MAME frame
and verifies that geometry/layering matches the MultiRex frame after palette
normalisation.  Palette normalisation is necessary for user-provided MKV/H.264
captures, which can shift RGB components by one LSB while preserving pixels.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
from typing import Optional

BASELINE_SHA256 = "401facdd7988b225920f7f66aaf3b0283a2d36ca793f6f8bc5ddabcf60d1553a"
BASELINE_FRAMES = 1791
BASELINE_MAME_FRAME = 1800


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


def count_black_hud_backdrop(width: int, height: int, pixels: bytes) -> tuple[int, int]:
    # Count black backdrop pixels in the left/right 16-pixel HUD bands, ignoring
    # top rows where score text changes between attract states.
    left = 0
    right = 0
    y0 = 24
    y1 = min(height, 204)
    for y in range(y0, y1):
        row = y * width * 3
        for x in range(0, min(16, width)):
            off = row + x * 3
            if pixels[off] == 0 and pixels[off + 1] == 0 and pixels[off + 2] == 0:
                left += 1
        for x in range(max(0, width - 16), width):
            off = row + x * 3
            if pixels[off] == 0 and pixels[off + 1] == 0 and pixels[off + 2] == 0:
                right += 1
    return left, right


def extract_mame_frame(video: Path, frame_number_1_based: int, out_ppm: Path) -> None:
    # ffmpeg select uses zero-based frame indices.  The frame numbers reported by
    # the regression are one-based, matching image sequence extraction names.
    n = max(0, frame_number_1_based - 1)
    vf = f"select=eq(n\\,{n})"
    cmd = [
        "ffmpeg", "-y", "-v", "error", "-i", str(video),
        "-vf", vf, "-frames:v", "1", "-f", "image2", str(out_ppm),
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr or proc.stdout or f"ffmpeg failed with exit {proc.returncode}")
    if not out_ppm.exists() or out_ppm.stat().st_size == 0:
        raise RuntimeError(f"ffmpeg did not extract MAME frame {frame_number_1_based} from {video}")


def sqdist(a: tuple[int, int, int], b: tuple[int, int, int]) -> int:
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return dr * dr + dg * dg + db * db


def normalised_pixel_diff(ref_pixels: bytes, ours_pixels: bytes) -> int:
    if len(ref_pixels) != len(ours_pixels):
        raise ValueError("reference and emulator pixel buffers differ in size")
    ours_palette = sorted({tuple(ours_pixels[i:i+3]) for i in range(0, len(ours_pixels), 3)})
    ref_colours = {tuple(ref_pixels[i:i+3]) for i in range(0, len(ref_pixels), 3)}
    mapping: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    for c in ref_colours:
        mapping[c] = min(ours_palette, key=lambda p: sqdist(c, p))
    diff = 0
    for i in range(0, len(ref_pixels), 3):
        ref_c = mapping[tuple(ref_pixels[i:i+3])]
        ours_c = tuple(ours_pixels[i:i+3])
        if ref_c != ours_c:
            diff += 1
    return diff


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out-dir", default=Path("test-results/athena_attract"), type=Path)
    parser.add_argument("--frames", default=BASELINE_FRAMES, type=int)
    parser.add_argument("--baseline-sha256", default=BASELINE_SHA256)
    parser.add_argument("--mame-video", default=None, type=Path, help="optional Athena MAME MKV/MP4 reference")
    parser.add_argument("--mame-frame", default=BASELINE_MAME_FRAME, type=int, help="one-based MAME reference frame number")
    parser.add_argument("--max-normalized-diff", default=0, type=int)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    screenshot = args.out_dir / "athena_attract.ppm"
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
    left_black, right_black = count_black_hud_backdrop(width, height, pixels)
    left_sample = pixel_at(width, pixels, 0, min(96, height - 1)) if width and height else (0, 0, 0)
    right_sample = pixel_at(width, pixels, width - 1, min(96, height - 1)) if width and height else (0, 0, 0)

    normalized_diff: Optional[int] = None
    mame_ppm: Optional[Path] = None
    failures: list[str] = []
    if actual.lower() != args.baseline_sha256.lower():
        failures.append(f"screenshot SHA-256 mismatch: got {actual}, expected {args.baseline_sha256}")
    if width != 288 or height != 216:
        failures.append(f"unexpected Athena visible size: {width}x{height}, expected 288x216")
    if left_black < 900 or right_black < 900:
        failures.append(f"HUD backdrop not black enough: left={left_black}, right={right_black}")
    if left_sample != (0, 0, 0) or right_sample != (0, 0, 0):
        failures.append(f"side HUD edge samples are not black: left={left_sample}, right={right_sample}")

    if args.mame_video is not None:
        if not args.mame_video.exists():
            failures.append(f"MAME reference video not found: {args.mame_video}")
        else:
            try:
                mame_ppm = args.out_dir / f"mame_frame_{args.mame_frame:06d}.ppm"
                extract_mame_frame(args.mame_video, args.mame_frame, mame_ppm)
                rw, rh, ref_pixels = read_ppm(mame_ppm)
                if (rw, rh) != (width, height):
                    failures.append(f"MAME reference size {rw}x{rh} does not match emulator {width}x{height}")
                else:
                    normalized_diff = normalised_pixel_diff(ref_pixels, pixels)
                    if normalized_diff > args.max_normalized_diff:
                        failures.append(
                            f"MAME-normalized pixel diff={normalized_diff}, expected <= {args.max_normalized_diff}"
                        )
            except Exception as exc:  # keep test output actionable
                failures.append(f"MAME reference comparison failed: {exc}")

    ok = not failures
    if not args.quiet or not ok:
        print("Athena attract-mode MAME regression")
        print(f"rom={args.rom.name} frames={args.frames}")
        print(f"screenshot={screenshot}")
        print(f"sha256={actual}")
        print(f"hud_black_pixels_left={left_black}")
        print(f"hud_black_pixels_right={right_black}")
        print(f"left_edge_sample={left_sample}")
        print(f"right_edge_sample={right_sample}")
        if args.mame_video is not None:
            print(f"mame_video={args.mame_video}")
            print(f"mame_frame={args.mame_frame}")
            print(f"mame_extracted_ppm={mame_ppm}")
            print(f"mame_normalized_pixel_diff={normalized_diff}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
