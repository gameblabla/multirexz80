#!/usr/bin/env python3
"""
Block Gal headless regression and smoke test.

Runs a fixed non-interactive Sega System 1 Block Gal boot, writes a final PPM
screenshot and optional WAV, and can compare SHA-256 hashes for deterministic
performance/correctness profiling runs.
"""

from __future__ import annotations

import argparse
import audioop
import hashlib
import os
import subprocess
import sys
import tempfile
import wave


def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_pcm_wav(path: str) -> tuple[wave._wave_params, bytes]:
    with wave.open(path, "rb") as w:
        params = w.getparams()
        pcm = w.readframes(w.getnframes())
    return params, pcm


def require_wav_sane(path: str) -> str:
    params, pcm = read_pcm_wav(path)
    if params.nchannels != 2:
        raise AssertionError(f"expected stereo WAV, got {params.nchannels} channel(s)")
    if params.sampwidth != 2:
        raise AssertionError(f"expected 16-bit WAV, got {params.sampwidth * 8}-bit")
    if params.framerate <= 0 or params.nframes <= 0:
        raise AssertionError("WAV has no audio frames")
    if len(pcm) != params.nframes * params.nchannels * params.sampwidth:
        raise AssertionError("WAV PCM payload size does not match the header")
    if audioop.rms(pcm, params.sampwidth) == 0:
        raise AssertionError("WAV is silent")
    return hashlib.sha256(pcm).hexdigest()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run and compare a fixed Block Gal headless capture.")
    ap.add_argument("--binary", default="./multirexz80_headless", help="headless executable to run")
    ap.add_argument("--rom", required=True, help="path to blockgal.zip")
    ap.add_argument("--frames", type=int, default=3000, help="frames to run; 3000 is 50 seconds at 60 Hz")
    ap.add_argument("--screenshot", help="output PPM path; defaults to a temporary file")
    ap.add_argument("--wav", help="optional output WAV path")
    ap.add_argument("--baseline-screenshot-sha256", help="optional expected SHA-256 of the final PPM")
    ap.add_argument("--baseline-pcm-sha256", help="optional expected SHA-256 of the WAV PCM payload")
    ap.add_argument("--keep", action="store_true", help="keep temporary outputs")
    ap.add_argument("--timeout", type=float, default=180.0, help="subprocess timeout in seconds")
    ap.add_argument("--quiet", action="store_true", help="suppress the runner's stdout/stderr unless it fails")
    args = ap.parse_args(argv)

    if args.frames <= 0:
        raise SystemExit("--frames must be positive")
    if not os.path.exists(args.binary):
        raise SystemExit(f"headless binary not found: {args.binary}")
    if not os.path.exists(args.rom):
        raise SystemExit(f"ROM not found: {args.rom}")

    temp_paths: list[str] = []
    screenshot = args.screenshot
    if not screenshot:
        fd, screenshot = tempfile.mkstemp(prefix="blockgal_", suffix=".ppm")
        os.close(fd)
        temp_paths.append(screenshot)

    cmd = [args.binary, "--console", "system1", "--frames", str(args.frames), "--screenshot", screenshot, "--quiet"]
    if args.wav:
        cmd += ["--audio-wav", args.wav]
    cmd.append(args.rom)

    run = subprocess.run(
        cmd,
        stdout=subprocess.PIPE if args.quiet else None,
        stderr=subprocess.PIPE if args.quiet else None,
        text=True,
        timeout=args.timeout,
    )
    if run.returncode != 0:
        if args.quiet:
            sys.stderr.write(run.stdout or "")
            sys.stderr.write(run.stderr or "")
        raise SystemExit(run.returncode)

    screenshot_digest = sha256_file(screenshot)
    print(f"screenshot={screenshot}")
    print(f"screenshot_sha256={screenshot_digest}")
    if args.baseline_screenshot_sha256 and screenshot_digest.lower() != args.baseline_screenshot_sha256.lower():
        raise AssertionError(f"screenshot SHA-256 mismatch: got {screenshot_digest}, expected {args.baseline_screenshot_sha256}")

    if args.wav:
        pcm_digest = require_wav_sane(args.wav)
        print(f"wav={args.wav}")
        print(f"pcm_sha256={pcm_digest}")
        if args.baseline_pcm_sha256 and pcm_digest.lower() != args.baseline_pcm_sha256.lower():
            raise AssertionError(f"PCM SHA-256 mismatch: got {pcm_digest}, expected {args.baseline_pcm_sha256}")

    if not args.keep:
        for path in temp_paths:
            try:
                os.unlink(path)
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
