#!/usr/bin/env python3
"""Chopper SNK OPL audio regression against the supplied MAME capture.

This protects the mixer bug where the Chopper/GWAR-family OPL pair was mixed at
Ikari's louder route level and then boosted again whenever frontend filters were
enabled, producing clipping/noisy garbage that does not exist in MAME.
"""
from __future__ import annotations

import argparse
import hashlib
import math
from pathlib import Path
import subprocess
import wave
from typing import Optional

RAW_PCM_SHA256 = "f6875905aee71edf48cb3ead7101e79c38456d9053cae6e24da57ffbe092bd53"
FILTERED_PCM_SHA256 = "0c7d10cc377a1565d6513a9a8cde702d34590016ce72d32e8a8cebcdc6405e83"
FRAMES = 1800
RATE = 44100


def wav_bytes_sha256(path: Path) -> str:
    with wave.open(str(path), "rb") as w:
        data = w.readframes(w.getnframes())
    return hashlib.sha256(data).hexdigest()


def wav_stats(path: Path) -> dict[str, float]:
    import array
    with wave.open(str(path), "rb") as w:
        channels = w.getnchannels()
        rate = w.getframerate()
        frames = w.getnframes()
        raw = w.readframes(frames)
    samples = array.array("h")
    samples.frombytes(raw)
    if channels <= 0:
        raise ValueError(f"{path}: invalid channel count {channels}")
    mono = []
    for i in range(0, len(samples), channels):
        mono.append(sum(samples[i:i + channels]) / channels)
    if not mono:
        raise ValueError(f"{path}: empty WAV")
    rms = math.sqrt(sum(x * x for x in mono) / len(mono))
    mean = sum(mono) / len(mono)
    max_abs = max(abs(x) for x in mono)
    # Ignore first two seconds of boot silence for the music/noise envelope.
    start = min(len(mono), 2 * rate)
    active = mono[start:] if start < len(mono) else mono
    active_rms = math.sqrt(sum(x * x for x in active) / len(active))
    active_peak = max(abs(x) for x in active)
    return {
        "channels": float(channels),
        "rate": float(rate),
        "frames": float(frames),
        "rms": rms,
        "mean": mean,
        "max_abs": max_abs,
        "active_rms": active_rms,
        "active_peak": active_peak,
    }


def run_headless(binary: Path, rom: Path, out_wav: Path, filtered: bool) -> None:
    cmd = [
        str(binary),
        "--console", "psychos",
        "--frames", str(FRAMES),
        "--audio-wav", str(out_wav),
        "--quiet",
    ]
    if filtered:
        cmd[5:5] = ["--audio-highpass-hz", "220", "--audio-lowpass-hz", "5000", "--no-audio-limiter"]
    cmd.append(str(rom))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        print(proc.stdout, end="")
        print(proc.stderr, end="")
        raise RuntimeError(f"headless audio run failed with exit {proc.returncode}")
    if not out_wav.exists() or out_wav.stat().st_size == 0:
        raise RuntimeError(f"missing audio output: {out_wav}")


def extract_mame_audio(video: Path, out_wav: Path) -> None:
    cmd = [
        "ffmpeg", "-y", "-v", "error", "-i", str(video),
        "-vn", "-ac", "2", "-ar", str(RATE), "-f", "wav", str(out_wav),
    ]
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr or proc.stdout or f"ffmpeg failed with exit {proc.returncode}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--binary", type=Path, required=True)
    ap.add_argument("--rom", type=Path, required=True)
    ap.add_argument("--mame-video", type=Path, default=None)
    ap.add_argument("--out-dir", type=Path, default=Path("test-results/chopper_audio"))
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)
    args.out_dir.mkdir(parents=True, exist_ok=True)
    raw_wav = args.out_dir / "chopper_raw.wav"
    filtered_wav = args.out_dir / "chopper_frontend_filter.wav"
    failures: list[str] = []

    try:
        run_headless(binary, args.rom, raw_wav, filtered=False)
        run_headless(binary, args.rom, filtered_wav, filtered=True)
    except Exception as exc:
        failures.append(str(exc))

    raw_sha = filtered_sha = None
    raw_stats = filtered_stats = None
    if raw_wav.exists():
        raw_sha = wav_bytes_sha256(raw_wav)
        raw_stats = wav_stats(raw_wav)
        if raw_sha != RAW_PCM_SHA256:
            failures.append(f"raw PCM SHA-256 mismatch: got {raw_sha}, expected {RAW_PCM_SHA256}")
        if raw_stats["active_peak"] > 12000:
            failures.append(f"raw active peak too high: {raw_stats['active_peak']:.0f}")
    if filtered_wav.exists():
        filtered_sha = wav_bytes_sha256(filtered_wav)
        filtered_stats = wav_stats(filtered_wav)
        if filtered_sha != FILTERED_PCM_SHA256:
            failures.append(f"frontend-filter PCM SHA-256 mismatch: got {filtered_sha}, expected {FILTERED_PCM_SHA256}")
        if filtered_stats["active_peak"] > 12000:
            failures.append(f"frontend-filter active peak too high/clipped: {filtered_stats['active_peak']:.0f}")

    mame_wav: Optional[Path] = None
    mame_stats = None
    if args.mame_video is not None:
        try:
            mame_wav = args.out_dir / "mame_ref.wav"
            extract_mame_audio(args.mame_video, mame_wav)
            mame_stats = wav_stats(mame_wav)
            if raw_stats:
                ratio = raw_stats["active_rms"] / mame_stats["active_rms"] if mame_stats["active_rms"] else 0.0
                if ratio < 0.70 or ratio > 1.20:
                    failures.append(f"raw active RMS/MAME ratio {ratio:.3f} outside [0.70, 1.20]")
                if raw_stats["active_peak"] > mame_stats["active_peak"] * 1.10:
                    failures.append(f"raw active peak {raw_stats['active_peak']:.0f} exceeds MAME peak {mame_stats['active_peak']:.0f} by >10%")
            if filtered_stats:
                if filtered_stats["active_peak"] > mame_stats["active_peak"] * 1.10:
                    failures.append(f"frontend-filter active peak {filtered_stats['active_peak']:.0f} exceeds MAME peak {mame_stats['active_peak']:.0f} by >10%")
        except Exception as exc:
            failures.append(f"MAME audio comparison failed: {exc}")

    ok = not failures
    if not args.quiet or not ok:
        print("Chopper audio regression")
        print(f"rom={args.rom}")
        print(f"raw_wav={raw_wav} sha256={raw_sha} stats={raw_stats}")
        print(f"frontend_filter_wav={filtered_wav} sha256={filtered_sha} stats={filtered_stats}")
        if args.mame_video is not None:
            print(f"mame_video={args.mame_video}")
            print(f"mame_wav={mame_wav} stats={mame_stats}")
        for failure in failures:
            print(f"FAIL: {failure}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
