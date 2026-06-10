#!/usr/bin/env python3
"""SMS VDPTEST regression helper.

Runs VDPTEST.sms through the compact input script and compares deterministic
screenshots at the data/misc/sprite pages.  Game Gear mode deliberately is not
asserted here: VDPTEST has known GG hardware differences for several counters
and flags, so this script is the Master System-mode correctness guard.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

DEFAULT_HASHES = {
    300: "23812e71b18ccd16b081f49ae7c00322634086de396a21979120d8254d10ee5f",  # data test page
    500: "7c0bb674ac0ee22418e8142d4e7f09c79f77eb39b4a9c45f8cc8c66e4e11284b",  # misc timing page
    600: "28ceb705bbb3fe4b993c97fd27a1516ae722848ceb6287717f13830d5c17a479",  # sprite timing page
    900: "ad12b04050962e7df07a5c2c1d41438dd27bc6ef3f68c12bf211c780af48b715",  # counter values page
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run VDPTEST.sms SMS-mode regression screenshots.")
    ap.add_argument("--binary", type=Path, default=Path("./multirexz80_headless"))
    ap.add_argument("--rom-dir", type=Path, default=Path("roms"))
    ap.add_argument("--out-dir", type=Path, default=Path("test-results/vdptest_sms"))
    ap.add_argument("--rom", default="VDPTEST.sms")
    ap.add_argument("--input", default="VDPTEST.script")
    ap.add_argument("--frames", type=int, default=1200)
    ap.add_argument("--cadence", type=int, default=100)
    ap.add_argument("--update", action="store_true", help="print observed hashes instead of failing")
    args = ap.parse_args(argv)

    binary = args.binary.resolve()
    rom = (args.rom_dir / args.rom).resolve()
    script = (args.rom_dir / args.input).resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    missing = [str(p) for p in (binary, rom, script) if not p.exists()]
    if missing:
        print("Missing file(s): " + ", ".join(missing), file=sys.stderr)
        return 2

    prefix = out_dir / "vdptest_sms"
    wav = out_dir / "vdptest_sms.wav"
    cmd = [
        str(binary), "--console", "sms", "--frames", str(args.frames),
        "--input-playback", str(script),
        "--screenshot-prefix", str(prefix), "--screenshot-every", str(args.cadence),
        "--audio-wav", str(wav), "--quiet", str(rom),
    ]
    subprocess.run(cmd, check=True)

    failures: list[str] = []
    print("VDPTEST SMS screenshot regression")
    print("%-8s %-8s %s" % ("frame", "status", "sha256"))
    for frame, expected in sorted(DEFAULT_HASHES.items()):
        shot = out_dir / f"vdptest_sms_{frame:06d}.ppm"
        if not shot.exists():
            failures.append(f"frame {frame}: missing screenshot {shot}")
            print("%-8d %-8s %s" % (frame, "MISSING", shot.name))
            continue
        observed = sha256_file(shot)
        ok = args.update or observed == expected
        print("%-8d %-8s %s" % (frame, "ok" if ok else "FAIL", observed))
        if not ok:
            failures.append(f"frame {frame}: got {observed}, expected {expected}")

    if args.update:
        print("\nObserved hashes above. Update DEFAULT_HASHES after intentional VDP timing changes.")
        return 0
    if failures:
        print("\nVDPTEST SMS REGRESSION DETECTED")
        for f in failures:
            print("  -", f)
        return 1
    print("\nVDPTEST SMS regression passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
