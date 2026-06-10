#!/usr/bin/env python3
"""Run ZEXALL-SMS through the optional Emulicious/BGB debug console.

The default project-root workflow is:

    mkdir -p roms
    cp zexall.sms roms/
    make -f Makefile.headless test-zexall-debug-console

The test is intended for correctness runs rather than quick smoke tests.  It
stops early when the debug console prints "Tests complete".  Any printed CRC
mismatch marks the run as failed.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import time

FAIL_MARKERS = (
    " expected ",
    " CRC ",
)
PASS_MARKER = "Tests complete"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run ZEXALL-SMS and parse its Emulicious/BGB debug console output.")
    ap.add_argument("--binary", default="./multirexz80_headless", help="headless executable built with ENABLE_DEBUG_CONSOLE=1")
    ap.add_argument("--rom", default="roms/zexall.sms", help="path to zexall.sms")
    ap.add_argument("--max-frames", type=int, default=20_000_000, help="safety frame limit; the run normally stops on 'Tests complete'")
    ap.add_argument("--debug-log", help="debug console log path; default is a temporary file")
    ap.add_argument("--timeout", type=float, default=0.0, help="subprocess timeout in seconds; 0 disables it")
    ap.add_argument("--allow-incomplete", action="store_true", help="smoke-test mode: do not fail when the complete marker is absent")
    ap.add_argument("--quiet", action="store_true", help="suppress emulator stdout/stderr unless it fails")
    args = ap.parse_args(argv)

    binary = Path(args.binary)
    rom = Path(args.rom)
    if not binary.exists():
        raise SystemExit(f"headless binary not found: {binary}")
    if not rom.exists():
        raise SystemExit(f"ZEXALL ROM not found: {rom}")
    if args.max_frames <= 0:
        raise SystemExit("--max-frames must be positive")

    temp_path: Path | None = None
    if args.debug_log:
        debug_log = Path(args.debug_log)
    else:
        fd, name = tempfile.mkstemp(prefix="zexall_debug_", suffix=".log")
        Path(name).unlink(missing_ok=True)
        import os
        os.close(fd)
        debug_log = Path(name)
        temp_path = debug_log

    binary_arg = str(binary if binary.is_absolute() else (Path.cwd() / binary))

    cmd = [
        binary_arg,
        "--console", "sms",
        "--frames", str(args.max_frames),
        "--debug-console", str(debug_log),
        "--debug-console-stop", PASS_MARKER,
        "--quiet",
        str(rom),
    ]

    start = time.perf_counter()
    try:
        run = subprocess.run(
            cmd,
            stdout=subprocess.PIPE if args.quiet else None,
            stderr=subprocess.PIPE if args.quiet else None,
            timeout=None if args.timeout <= 0 else args.timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise SystemExit(f"ZEXALL run timed out after {args.timeout:.1f}s; partial log: {debug_log}") from exc
    seconds = time.perf_counter() - start

    if run.returncode != 0:
        if args.quiet:
            sys.stderr.write(run.stdout.decode(errors="replace"))
            sys.stderr.write(run.stderr.decode(errors="replace"))
        raise SystemExit(f"ZEXALL runner failed with exit code {run.returncode}; log: {debug_log}")

    text = debug_log.read_text(errors="replace") if debug_log.exists() else ""
    failed_markers = [m for m in FAIL_MARKERS if m in text]
    complete = PASS_MARKER in text

    status = "PASS" if complete and not failed_markers else "FAIL"
    if args.allow_incomplete and not failed_markers:
        status = "SMOKE" if not complete else "PASS"

    line_count = text.count("\n") + (1 if text and not text.endswith("\n") else 0)
    print(f"zexall: status={status} complete={str(complete).lower()} seconds={seconds:.3f} lines={line_count} log={debug_log}")

    if failed_markers:
        print("ZEXALL failure marker(s) found:", ", ".join(repr(m) for m in failed_markers), file=sys.stderr)
        print_tail(text)
        return 1
    if not complete and not args.allow_incomplete:
        print("ZEXALL did not reach the completion marker before the safety frame limit.", file=sys.stderr)
        print_tail(text)
        return 1

    if temp_path is not None:
        temp_path.unlink(missing_ok=True)
    return 0


def print_tail(text: str, lines: int = 40) -> None:
    tail = text.splitlines()[-lines:]
    if tail:
        print("--- debug console tail ---", file=sys.stderr)
        for line in tail:
            print(line, file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
