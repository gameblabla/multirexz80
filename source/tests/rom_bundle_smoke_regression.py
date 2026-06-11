#!/usr/bin/env python3
"""Smoke-test every supported ROM in a local test bundle.

This is intentionally looser than the SHA-256 regression suite.  It catches
loader/renderer regressions where a title boots to a blank frame, crashes, or is
forced through the wrong machine profile.  It is useful for mixed ROM bundles
that contain SMS/GG files, zipped SMS/GG files, and arcade ZIP sets.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import time

ARCADE_CONSOLES: dict[str, str] = {
    "blockgal.zip": "system1",
    "blockgalb.zip": "system1",
    "choplift.zip": "system1",
    "flicky.zip": "system1",
    "gardia.zip": "system1",
    "teddybb.zip": "system1",
    "tetrisse.zip": "systeme",
    "transfrm.zip": "systeme",
    "ikari.zip": "psychos",
    "psychos.zip": "psychos",
    "psychosj.zip": "psychos",
    "victroad.zip": "psychos",
    "dogosoke.zip": "psychos",
    "dogosokb.zip": "psychos",
    "gwar.zip": "psychos",
    "gwarj.zip": "psychos",
    "chopper.zip": "psychos",
    "choppera.zip": "psychos",
    "chopperb.zip": "psychos",
    "legofair.zip": "psychos",
    "tdfever.zip": "psychos",
    "tdfeverj.zip": "psychos",
    "athena.zip": "psychos",
}

# ZIPs that are packaging containers with a duplicate .sms/.gg file present in
# the same bundle, or alternate archives we do not want to count twice.  The
# loose loader path only extracts the first ZIP member, so a ZIP with README.txt
# first is deliberately tested via its sibling .sms file instead.
DEFAULT_SKIPS = {
    "TablesHaveTurned-SMS-1.00.zip",
    "tetrisse-1.zip",
}


def ppm_stats(path: Path) -> tuple[int, int, int, int, str]:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise ValueError(f"{path}: not a binary PPM")
    dims_end = data.find(b"\n", 3)
    width, height = map(int, data[3:dims_end].split())
    max_end = data.find(b"\n", dims_end + 1)
    pixels = data[max_end + 1:]
    if len(pixels) != width * height * 3:
        raise ValueError(f"{path}: unexpected pixel payload size")
    nonblack = sum(1 for i in range(0, len(pixels), 3) if pixels[i:i + 3] != b"\x00\x00\x00")
    unique = len({pixels[i:i + 3] for i in range(0, len(pixels), 3)})
    return width, height, nonblack, unique, hashlib.sha256(data).hexdigest()


def discover_roms(rom_dir: Path) -> list[Path]:
    result: list[Path] = []
    for p in sorted(rom_dir.iterdir(), key=lambda x: x.name.lower()):
        if p.name in DEFAULT_SKIPS:
            continue
        if p.suffix.lower() in {".sms", ".gg", ".zip"}:
            result.append(p)
    return result


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Smoke-test every supported ROM in a mixed ROM bundle.")
    ap.add_argument("--binary", type=Path, default=Path("./multirexz80_headless"))
    ap.add_argument("--rom-dir", type=Path, required=True)
    ap.add_argument("--out-dir", type=Path, default=Path("test-results/rom_bundle_smoke"))
    ap.add_argument("--frames", type=int, default=3600)
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("--min-nonblack", type=int, default=100)
    args = ap.parse_args(argv)

    binary = args.binary.resolve()
    rom_dir = args.rom_dir.resolve()
    out_dir = args.out_dir.resolve()
    if not binary.exists():
        raise SystemExit(f"headless binary not found: {binary}")
    if not rom_dir.is_dir():
        raise SystemExit(f"ROM directory not found: {rom_dir}")
    out_dir.mkdir(parents=True, exist_ok=True)

    failures: list[str] = []
    roms = discover_roms(rom_dir)
    print(f"ROM bundle smoke regression: {len(roms)} file(s), frames={args.frames}")
    print("%-54s %-8s %-6s %-9s %-9s %-6s %s" % ("file", "console", "result", "seconds", "nonblack", "uniq", "sha256"), flush=True)

    for rom in roms:
        console = ARCADE_CONSOLES.get(rom.name, "auto")
        if rom.name == "ZEXALL-SMS-0.21.zip":
            console = "sms"
        safe = "".join(c if c.isalnum() else "_" for c in rom.stem)[:96]
        ppm = out_dir / f"{safe}.ppm"
        cmd = [str(binary), "--console", console, "--frames", str(args.frames), "--screenshot", str(ppm), "--quiet", str(rom)]
        start = time.perf_counter()
        try:
            proc = subprocess.run(cmd, timeout=args.timeout, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except subprocess.TimeoutExpired:
            failures.append(f"{rom.name}: timeout after {args.timeout:.1f}s")
            print("%-54s %-8s %-6s %-9s %-9s %-6s %s" % (rom.name, console, "FAIL", "timeout", "-", "-", "timeout"), flush=True)
            continue
        elapsed = time.perf_counter() - start
        if proc.returncode != 0 or not ppm.exists():
            msg = "emulator returned non-zero status"
            failures.append(f"{rom.name}: emulator exited {proc.returncode}: {msg}")
            print("%-54s %-8s %-6s %-9.3f %-9s %-6s %s" % (rom.name, console, "FAIL", elapsed, "-", "-", msg), flush=True)
            continue
        try:
            w, h, nonblack, unique, sha = ppm_stats(ppm)
        except Exception as exc:
            failures.append(f"{rom.name}: {exc}")
            print("%-54s %-8s %-6s %-9.3f %-9s %-6s %s" % (rom.name, console, "FAIL", elapsed, "-", "-", str(exc)), flush=True)
            continue
        ok = (w > 0 and h > 0 and unique >= 2 and nonblack >= args.min_nonblack)
        if not ok:
            failures.append(f"{rom.name}: suspicious blank/simple frame ({w}x{h}, nonblack={nonblack}, unique={unique}, sha256={sha})")
        print("%-54s %-8s %-6s %-9.3f %-9d %-6d %s" % (rom.name, console, "ok" if ok else "FAIL", elapsed, nonblack, unique, sha), flush=True)

    if failures:
        print("\nSMOKE REGRESSION DETECTED")
        for f in failures:
            print("  -", f)
        return 1
    print("\nAll ROM bundle smoke checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
