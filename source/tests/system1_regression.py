#!/usr/bin/env python3
"""Sega System 1 headless regression suite.

The default workflow is intended for a normal PC checkout:

    python3 source/tests/system1_regression.py

It builds Makefile.headless, looks for ROM ZIP files in ./roms next to the
Makefile, runs each game long enough to get past startup/self-check screens,
and compares both final video (PPM SHA-256) and audio (WAV PCM SHA-256).
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import time
import wave

GAMES = {
    "blockgal": {
        "rom": "blockgal.zip",
        "console": "system1",
        "screenshot_sha256": "723dc2ecc2f687188acd7771fa15c43daa9612b65bc2078ae2a2a6c501fafcf1",
        "pcm_sha256": "8f48c9b990c2b9795121beb0b3f6c2db2ffd1ff4561d8384b83a7450e5efe117",
    },
    "blockgalb": {
        "rom": "blockgalb.zip",
        "console": "system1",
        "screenshot_sha256": "f268e9645b50088746cf8a3406394c151d5b51092daf5a5c89d385feba2da675",
        "pcm_sha256": "fd907f65c2903627e9ca4bc81e449b307d9e61334a4a2fdcfd24cd5cb6dee5fc",
    },
    "choplift": {
        "rom": "choplift.zip",
        "console": "system1",
        "screenshot_sha256": "435a6718cd5ff6280f508fa692e784967418ff2d69c8b552733d8208eec2cdde",
        "pcm_sha256": "f95cb4c22ce6764c8870bfddf49fddfd63d34174ac4ae870137ddb0632ca15b3",
    },
    "flicky": {
        "rom": "flicky.zip",
        "console": "system1",
        "screenshot_sha256": "1d079273592a9365c4f096cc201b6ff749fb7d0cf466f401b7cf0bc6acb39b94",
        "pcm_sha256": "04c70ed0b47b56cdc2263435922022fcfbecd5f240861a83e675abc6c0971669",
    },
    "gardia": {
        "rom": "gardia.zip",
        "console": "system1",
        "screenshot_sha256": "5d183a8a1faf0441be54c39af35ef3e20ee06aebaa7d76fad7a2d8401040b5aa",
        "pcm_sha256": "b92a66a8855e0ddc5742280d565fb7db891e8114f43526952e53958798fb2ee9",
    },
    "teddybb": {
        "rom": "teddybb.zip",
        "console": "system1",
        "screenshot_sha256": "c65e2a00e1ce211724b6f9bbc2412f5ef7b51b6deaff8ed6bde19a9bb1108448",
        "pcm_sha256": "90be5f138064d59415d916d6c461d0d00c6ed465cfa5e6108249e3983418c158",
    },
}


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
    if params.nchannels != 2:
        raise AssertionError(f"{path}: expected stereo WAV, got {params.nchannels} channel(s)")
    if params.sampwidth != 2:
        raise AssertionError(f"{path}: expected 16-bit WAV, got {params.sampwidth * 8}-bit")
    if params.framerate <= 0 or params.nframes <= 0:
        raise AssertionError(f"{path}: WAV has no audio frames")
    return hashlib.sha256(pcm).hexdigest(), f"{params.nframes}f/{params.framerate}Hz"


def run_checked(cmd: list[str], cwd: Path, timeout: float | None = None) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd), timeout=timeout)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def build_headless(root: Path, makefile: str, jobs: int) -> None:
    cmd = ["make", "-f", makefile, f"-j{jobs}"]
    print("Building headless:", " ".join(cmd))
    run_checked(cmd, root)


def main(argv: list[str]) -> int:
    root_default = project_root_from_script()
    ap = argparse.ArgumentParser(description="Build and run deterministic Sega System 1 headless audio/video regressions.")
    ap.add_argument("--root", type=Path, default=root_default, help="project root; defaults to the directory containing Makefile.headless")
    ap.add_argument("--makefile", default="Makefile.headless", help="headless Makefile name")
    ap.add_argument("--binary", default="./multirexz80_headless", help="headless binary path, relative to --root unless absolute")
    ap.add_argument("--rom-dir", type=Path, default=None, help="ROM directory; defaults to ./roms next to the Makefile")
    ap.add_argument("--out-dir", type=Path, default=None, help="output directory; defaults to ./test-results/system1_regression")
    ap.add_argument("--frames", type=int, default=3600, help="frames per game; default is 3600, about 60 seconds at 60 Hz")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="make parallelism")
    ap.add_argument("--no-build", action="store_true", help="do not build before running")
    ap.add_argument("--games", nargs="+", choices=sorted(GAMES), default=list(GAMES), help="subset of games to run")
    ap.add_argument("--timeout", type=float, default=180.0, help="per-game timeout in seconds")
    args = ap.parse_args(argv)

    root = args.root.resolve()
    rom_dir = (args.rom_dir if args.rom_dir is not None else root / "roms").resolve()
    out_dir = (args.out_dir if args.out_dir is not None else root / "test-results" / "system1_regression").resolve()
    binary = Path(args.binary)
    if not binary.is_absolute():
        binary = root / binary

    if args.frames <= 0:
        raise SystemExit("--frames must be positive")
    if not (root / args.makefile).exists():
        raise SystemExit(f"Makefile not found: {root / args.makefile}")
    if not rom_dir.is_dir():
        raise SystemExit(f"ROM directory not found: {rom_dir}")

    missing = [spec["rom"] for name, spec in GAMES.items() if name in args.games and not (rom_dir / spec["rom"]).exists()]
    if missing:
        raise SystemExit("Missing ROM(s) in %s: %s" % (rom_dir, ", ".join(missing)))

    if not args.no_build:
        build_headless(root, args.makefile, args.jobs)
    if not binary.exists():
        raise SystemExit(f"headless binary not found: {binary}")

    out_dir.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    print("\nSega System 1 regression, frames=%d" % args.frames)
    print("%-10s %-9s %-9s %-10s %s" % ("game", "video", "audio", "seconds", "outputs"))

    for name in args.games:
        spec = GAMES[name]
        ppm = out_dir / f"{name}.ppm"
        wav = out_dir / f"{name}.wav"
        rom = rom_dir / spec["rom"]
        cmd = [str(binary), "--console", spec["console"], "--frames", str(args.frames), "--screenshot", str(ppm), "--audio-wav", str(wav), "--quiet", str(rom)]
        start = time.perf_counter()
        try:
            run_checked(cmd, root, timeout=args.timeout)
            elapsed = time.perf_counter() - start
            video_sha = sha256_file(ppm)
            audio_sha, wav_desc = pcm_sha256_wav(wav)
            video_ok = video_sha == spec["screenshot_sha256"]
            audio_ok = audio_sha == spec["pcm_sha256"]
            if not video_ok:
                failures.append(f"{name}: screenshot SHA-256 mismatch: got {video_sha}, expected {spec['screenshot_sha256']}")
            if not audio_ok:
                failures.append(f"{name}: PCM SHA-256 mismatch: got {audio_sha}, expected {spec['pcm_sha256']}")
            print("%-10s %-9s %-9s %-10.3f %s, %s" % (name, "ok" if video_ok else "FAIL", "ok" if audio_ok else "FAIL", elapsed, ppm.name, wav_desc))
        except Exception as exc:  # keep reporting the whole suite when possible
            failures.append(f"{name}: {exc}")
            print("%-10s %-9s %-9s %-10s %s" % (name, "FAIL", "FAIL", "-", exc))

    if failures:
        print("\nREGRESSION DETECTED")
        for f in failures:
            print("  -", f)
        return 1
    print("\nAll System 1 audio/video regressions passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
