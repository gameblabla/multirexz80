#!/usr/bin/env python3
"""ColecoVision TMS9918 multicolor timing regression.

Runs vm_multicolor.cv with the real ColecoVision BIOS and verifies that the
active-display register-4 timing no longer leaves an oversized/glitchy final
multicolor row.
"""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from pathlib import Path

BASELINE_SHA256 = "7c0bf269a685f460da56708824bdb513f01f953060bf474f2ef625481aa81c2d"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--bios", required=True, type=Path)
    parser.add_argument("--out-dir", default=Path("test-results/coleco_vm_multicolor"), type=Path)
    parser.add_argument("--frames", default=80, type=int)
    parser.add_argument("--baseline-sha256", default=BASELINE_SHA256)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    screenshot = args.out_dir / "vm_multicolor.ppm"
    if screenshot.exists():
        screenshot.unlink()

    binary = args.binary if args.binary.is_absolute() else (Path.cwd() / args.binary)

    cmd = [
        str(binary),
        "--console", "coleco",
        "--coleco-bios", str(args.bios),
        "--frames", str(args.frames),
        "--screenshot", str(screenshot),
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
    ok = actual.lower() == args.baseline_sha256.lower()
    if not args.quiet or not ok:
        print("ColecoVision vm_multicolor regression")
        print(f"rom={args.rom.name} bios={args.bios.name} frames={args.frames}")
        print(f"screenshot={screenshot}")
        print(f"sha256={actual}")
        print("status=" + ("ok" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
