#!/usr/bin/env python3
"""SMS/GG VDP sprite-latch regression helper.

This is intended for cases such as Madou Monogatari I GG where a save state is
needed to reach a scene that relies on sprite size/shift latching.  The ROM and
state are not distributed by this project; place them under ./roms or pass
explicit paths.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import time
import wave


def project_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def pcm_sha256_wav(path: Path) -> tuple[str, str]:
    with wave.open(str(path), "rb") as w:
        params = w.getparams()
        pcm = w.readframes(w.getnframes())
    if params.nchannels != 2 or params.sampwidth != 2 or params.framerate <= 0 or params.nframes <= 0:
        raise AssertionError(f"{path}: invalid WAV parameters: {params!r}")
    return hashlib.sha256(pcm).hexdigest(), f"{params.nframes}f/{params.framerate}Hz"


def main() -> int:
    root = project_root_from_script()
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", default=str(root / "multirexz80_headless"))
    ap.add_argument("--rom-dir", default=str(root / "roms"))
    ap.add_argument("--rom", default=None, help="GG/SMS ROM path; defaults to Madou Monogatari I GG in --rom-dir")
    ap.add_argument("--load-state", required=True, help="PNG/.sgxst state captured at the problematic scene")
    ap.add_argument("--console", default="gg")
    ap.add_argument("--frames", type=int, default=120, help="frames to run after loading the state")
    ap.add_argument("--out-dir", default=str(root / "test-results" / "sms_vdp_latch"))
    ap.add_argument("--screenshot-sha256", help="expected final PPM SHA-256")
    ap.add_argument("--pcm-sha256", help="expected WAV PCM SHA-256")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    if args.frames <= 0:
        raise SystemExit("--frames must be positive")

    binary = Path(args.binary)
    if not binary.is_absolute():
        binary = (root / binary).resolve()
    rom_dir = Path(args.rom_dir)
    if not rom_dir.is_absolute():
        rom_dir = (root / rom_dir).resolve()
    if args.rom:
        rom = Path(args.rom)
        if not rom.is_absolute():
            rom = (root / rom).resolve()
    else:
        candidates = [
            rom_dir / "Madou Monogatari I - 3tsu no Madoukyuu (Japan).gg",
            rom_dir / "madou1.gg",
            rom_dir / "madou.gg",
        ]
        rom = next((p for p in candidates if p.exists()), candidates[0])
    state = Path(args.load_state)
    if not state.is_absolute():
        state = (root / state).resolve()
    out_dir = Path(args.out_dir)
    if not out_dir.is_absolute():
        out_dir = (root / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    ppm = out_dir / "sms_vdp_latch.ppm"
    wav = out_dir / "sms_vdp_latch.wav"

    missing = [str(p) for p in (binary, rom, state) if not p.exists()]
    if missing:
        raise SystemExit("missing required file(s): " + ", ".join(missing))

    cmd = [
        str(binary),
        "--console", args.console,
        "--load-state", str(state),
        "--frames", str(args.frames),
        "--screenshot", str(ppm),
        "--audio-wav", str(wav),
        "--quiet",
        str(rom),
    ]
    start = time.perf_counter()
    subprocess.run(cmd, check=True)
    elapsed = time.perf_counter() - start

    video_sha = sha256_file(ppm)
    pcm_sha, wav_desc = pcm_sha256_wav(wav)

    failures: list[str] = []
    if args.screenshot_sha256 and video_sha.lower() != args.screenshot_sha256.lower():
        failures.append(f"screenshot SHA-256 mismatch: got {video_sha}, expected {args.screenshot_sha256}")
    if args.pcm_sha256 and pcm_sha.lower() != args.pcm_sha256.lower():
        failures.append(f"WAV PCM SHA-256 mismatch: got {pcm_sha}, expected {args.pcm_sha256}")

    if not args.quiet:
        print(f"video_sha256={video_sha}")
        print(f"pcm_sha256={pcm_sha} ({wav_desc})")
        print(f"elapsed={elapsed:.3f}s outputs={ppm}, {wav}")

    if failures:
        print("REGRESSION DETECTED", file=sys.stderr)
        for item in failures:
            print(item, file=sys.stderr)
        return 1

    print("SMS/GG VDP latch regression passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
