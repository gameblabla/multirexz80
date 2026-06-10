#!/usr/bin/env python3
"""
Psychos headless audio regression test.

Run a non-interactive Psycho Soldier/Psychos boot for a fixed number of frames,
write the headless WAV stream, and optionally compare it against a baseline WAV
or a known PCM SHA-256.  The comparison hashes the PCM payload, not the RIFF
header, so timestamp- or tool-specific metadata cannot affect the result.
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


def read_pcm_wav(path: str) -> tuple[wave._wave_params, bytes]:
    with wave.open(path, "rb") as w:
        params = w.getparams()
        pcm = w.readframes(w.getnframes())
    return params, pcm


def pcm_sha256(pcm: bytes) -> str:
    return hashlib.sha256(pcm).hexdigest()


def require_wav_sane(path: str) -> tuple[wave._wave_params, bytes, str]:
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
    return params, pcm, pcm_sha256(pcm)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run and compare a 50-second Psychos headless audio capture.")
    ap.add_argument("--binary", default="./multirexz80_headless", help="headless executable to run")
    ap.add_argument("--rom", required=True, help="path to psychos.zip")
    ap.add_argument("--frames", type=int, default=3000, help="frames to run; 3000 is 50 seconds at 60 Hz")
    ap.add_argument("--wav", help="output WAV path; defaults to a temporary file")
    ap.add_argument("--baseline-wav", help="optional baseline WAV to compare against")
    ap.add_argument("--baseline-pcm-sha256", help="optional expected SHA-256 of the baseline PCM payload")
    ap.add_argument("--keep-wav", action="store_true", help="keep the temporary WAV when --wav was not supplied")
    ap.add_argument("--timeout", type=float, default=180.0, help="subprocess timeout in seconds")
    ap.add_argument("--quiet", action="store_true", help="suppress the runner's stdout/stderr unless it fails")
    args = ap.parse_args(argv)

    if args.frames <= 0:
        raise SystemExit("--frames must be positive")
    if not os.path.exists(args.binary):
        raise SystemExit(f"headless binary not found: {args.binary}")
    if not os.path.exists(args.rom):
        raise SystemExit(f"ROM not found: {args.rom}")

    temp = None
    wav_path = args.wav
    if not wav_path:
        fd, wav_path = tempfile.mkstemp(prefix="psychos_audio_", suffix=".wav")
        os.close(fd)
        temp = wav_path

    cmd = [args.binary, "--console", "psychos", "--frames", str(args.frames), "--audio-wav", wav_path, "--quiet", args.rom]
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

    params, pcm, digest = require_wav_sane(wav_path)
    print(f"wav={wav_path}")
    print(f"frames={params.nframes} rate={params.framerate} channels={params.nchannels} pcm_sha256={digest}")

    if args.baseline_wav:
        bparams, bpcm, bdigest = require_wav_sane(args.baseline_wav)
        if params[:4] != bparams[:4]:
            raise AssertionError(f"WAV format mismatch: got {params}, baseline {bparams}")
        if pcm != bpcm:
            raise AssertionError(f"PCM mismatch: got {digest}, baseline {bdigest}")
        print(f"baseline_wav_pcm_sha256={bdigest}")

    if args.baseline_pcm_sha256:
        expected = args.baseline_pcm_sha256.lower()
        if digest.lower() != expected:
            raise AssertionError(f"PCM SHA-256 mismatch: got {digest}, expected {expected}")

    if temp and not args.keep_wav:
        os.unlink(temp)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
