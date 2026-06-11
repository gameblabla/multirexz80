#!/usr/bin/env python3
"""Psycho Soldier 16x16 sprite ROM decode/rendering regression.

This protects the SNK Psycho Soldier sprite path fixed after comparison against
MAME: the title-screen spinning Y and gameplay 16x16 objects/orbs must remain
stable.  The default oracle is deterministic MultiRex frame hashing at two
frames.  When --mame-video is supplied, the title frame is also compared against
a MAME capture with palette normalisation, which tolerates small RGB shifts from
lossy MKV/H.264 captures while still catching layout/plane/order regressions.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
from typing import Optional

BASELINES: dict[int, str] = {
    300: "d8b70c63396f371f2143e099d9dee7ccd6d06bc5e22a59dc0e509eeb2c418e19",
    3600: "aced9ac000a96edeacd3f79f4741e42b0484577d97e442151a42130b4d355b8b",
}
BASELINE_GEOMETRY = (400, 224)


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


def extract_mame_frame(video: Path, frame_number_1_based: int, out_ppm: Path) -> None:
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
    ours_palette = sorted({tuple(ours_pixels[i:i + 3]) for i in range(0, len(ours_pixels), 3)})
    ref_colours = {tuple(ref_pixels[i:i + 3]) for i in range(0, len(ref_pixels), 3)}
    mapping: dict[tuple[int, int, int], tuple[int, int, int]] = {}
    for c in ref_colours:
        mapping[c] = min(ours_palette, key=lambda p: sqdist(c, p))
    diff = 0
    for i in range(0, len(ref_pixels), 3):
        if mapping[tuple(ref_pixels[i:i + 3])] != tuple(ours_pixels[i:i + 3]):
            diff += 1
    return diff


def run_frame(binary: Path, rom: Path, out_dir: Path, frames: int) -> tuple[Path, str, int, int, bytes]:
    screenshot = out_dir / f"psychos_{frames}.ppm"
    if screenshot.exists():
        screenshot.unlink()
    cmd = [
        str(binary),
        "--console", "psychos",
        "--frames", str(frames),
        "--screenshot", str(screenshot),
        "--quiet",
        str(rom),
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        print(proc.stdout, end="")
        print(proc.stderr, end="")
        raise RuntimeError(f"headless run failed at frame {frames} with exit {proc.returncode}")
    if not screenshot.exists():
        raise RuntimeError(f"missing screenshot: {screenshot}")
    digest = sha256_file(screenshot)
    width, height, pixels = read_ppm(screenshot)
    return screenshot, digest, width, height, pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--out-dir", default=Path("test-results/psychos_sprite"), type=Path)
    parser.add_argument("--frames", nargs="+", type=int, default=sorted(BASELINES), help="frame numbers to validate")
    parser.add_argument("--mame-video", default=None, type=Path, help="optional Psycho Soldier MAME MKV/MP4 capture")
    parser.add_argument("--mame-title-frame", default=300, type=int, help="one-based MAME title frame for optional comparison")
    parser.add_argument("--max-title-normalized-diff", default=1000, type=int)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)
    args.out_dir.mkdir(parents=True, exist_ok=True)

    failures: list[str] = []
    outputs: list[tuple[int, Path, str, int, int]] = []
    pixels_by_frame: dict[int, bytes] = {}
    geometry_by_frame: dict[int, tuple[int, int]] = {}

    for frames in args.frames:
        try:
            screenshot, digest, width, height, pixels = run_frame(binary, args.rom, args.out_dir, frames)
            outputs.append((frames, screenshot, digest, width, height))
            pixels_by_frame[frames] = pixels
            geometry_by_frame[frames] = (width, height)
            expected = BASELINES.get(frames)
            if expected is None:
                failures.append(f"no built-in baseline for frame {frames}")
            elif digest.lower() != expected.lower():
                failures.append(f"frame {frames}: screenshot SHA-256 mismatch: got {digest}, expected {expected}")
            if (width, height) != BASELINE_GEOMETRY:
                failures.append(f"frame {frames}: unexpected geometry {width}x{height}, expected {BASELINE_GEOMETRY[0]}x{BASELINE_GEOMETRY[1]}")
        except Exception as exc:
            failures.append(f"frame {frames}: {exc}")

    mame_title_ppm: Optional[Path] = None
    mame_title_diff: Optional[int] = None
    if args.mame_video is not None:
        if not args.mame_video.exists():
            failures.append(f"MAME reference video not found: {args.mame_video}")
        elif 300 not in pixels_by_frame:
            failures.append("MAME comparison requires emulator frame 300 in --frames")
        else:
            try:
                mame_title_ppm = args.out_dir / f"mame_title_{args.mame_title_frame:06d}.ppm"
                extract_mame_frame(args.mame_video, args.mame_title_frame, mame_title_ppm)
                rw, rh, ref_pixels = read_ppm(mame_title_ppm)
                ew, eh = geometry_by_frame[300]
                if (rw, rh) != (ew, eh):
                    failures.append(f"MAME title reference size {rw}x{rh} does not match emulator {ew}x{eh}")
                else:
                    mame_title_diff = normalised_pixel_diff(ref_pixels, pixels_by_frame[300])
                    if mame_title_diff > args.max_title_normalized_diff:
                        failures.append(
                            f"MAME title normalized pixel diff={mame_title_diff}, expected <= {args.max_title_normalized_diff}"
                        )
            except Exception as exc:
                failures.append(f"MAME title comparison failed: {exc}")

    ok = not failures
    if not args.quiet or not ok:
        print("Psycho Soldier sprite regression")
        print(f"rom={args.rom}")
        for frames, screenshot, digest, width, height in outputs:
            print(f"frame={frames} screenshot={screenshot} geometry={width}x{height} sha256={digest}")
        if args.mame_video is not None:
            print(f"mame_video={args.mame_video}")
            print(f"mame_title_frame={args.mame_title_frame}")
            print(f"mame_title_ppm={mame_title_ppm}")
            print(f"mame_title_normalized_pixel_diff={mame_title_diff}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
