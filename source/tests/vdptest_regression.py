#!/usr/bin/env python3
"""SMS VDPTEST regression helper.

Runs VDPTEST.sms through its compact input script and compares deterministic
screenshots after the scripted page transitions have completed.  Game Gear mode
deliberately is not asserted here: VDPTEST has known GG hardware differences
for several counters and flags, so this script is the Master System-mode
correctness guard.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys
import zlib
from typing import Optional

NTSC_HASHES = {
    # The VDPTEST input script advances through several short-lived pages.
    # Capture after the transition frames have completed, not on the grey
    # interstitials between tests or after the script has already returned to
    # the menu.
    300: "23812e71b18ccd16b081f49ae7c00322634086de396a21979120d8254d10ee5f",  # SMS VDP data test results
    430: "7c0bb674ac0ee22418e8142d4e7f09c79f77eb39b4a9c45f8cc8c66e4e11284b",  # SMS VDP misc test results
    520: "28ceb705bbb3fe4b993c97fd27a1516ae722848ceb6287717f13830d5c17a479",  # SMS VDP sprite test results; frame 500 is still a grey transition
    600: "0aa317e99c49d2725bb600adbd6122291d42e48823471567d4dbcdfa1e30f97c",  # X-scroll latch-time visual check
    700: "74eefaee3199fc2abcf31b77b80d1acdf220ec3c8126866dcd81d6aa156d4572",  # HCounter values; frame 900 has already returned to the menu
}

PAL_HASHES = {
    # Same scripted VDPTEST sequence in SMS PAL/50 Hz mode.  The misc page
    # intentionally says PAL and the HCounter page reports the PAL sequence.
    300: "23812e71b18ccd16b081f49ae7c00322634086de396a21979120d8254d10ee5f",
    430: "a1560d443e8d72d45abac26796c28816172f061569905005e0e26af95e335253",
    520: "28ceb705bbb3fe4b993c97fd27a1516ae722848ceb6287717f13830d5c17a479",
    600: "0aa317e99c49d2725bb600adbd6122291d42e48823471567d4dbcdfa1e30f97c",
    700: "9356768acd53695c69b32a410a96966564879a6881e4213825c3f78d5fbd185c",
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def crc32_file(path: Path) -> int:
    crc = 0
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            crc = zlib.crc32(chunk, crc)
    return crc & 0xFFFFFFFF


def resolve_rom(rom_dir: Path, rom_name: str) -> Path:
    rom_path = Path(rom_name)
    if not rom_path.is_absolute():
        rom_path = rom_dir / rom_path
    return rom_path.resolve()


def resolve_input_script(rom_dir: Path, rom: Path, input_name: Optional[str]) -> tuple[Optional[Path], list[str]]:
    candidates: list[str] = []
    if input_name:
        candidates.append(input_name)
    else:
        rom_crc = crc32_file(rom) if rom.exists() else 0
        if rom_crc:
            candidates.append(f"{rom.stem}_{rom_crc:08X}.input")
        candidates += [f"{rom.stem}.input", f"{rom.stem}.script", "VDPTEST_2F7D2CEA.input"]

    seen: set[str] = set()
    checked: list[str] = []
    for candidate in candidates:
        if candidate in seen:
            continue
        seen.add(candidate)
        p = Path(candidate)
        if not p.is_absolute():
            p = rom_dir / p
        p = p.resolve()
        checked.append(str(p))
        if p.exists():
            return p, checked
    return None, checked


def remove_stale_screenshots(out_dir: Path, prefix_name: str) -> None:
    for stale in out_dir.glob(f"{prefix_name}_*.ppm"):
        stale.unlink()
    wav = out_dir / f"{prefix_name}.wav"
    if wav.exists():
        wav.unlink()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run VDPTEST.sms SMS-mode regression screenshots.")
    ap.add_argument("--binary", type=Path, default=Path("./multirexz80_headless"))
    ap.add_argument("--rom-dir", type=Path, default=Path("roms"))
    ap.add_argument("--out-dir", type=Path, default=Path("test-results/vdptest_sms"))
    ap.add_argument("--rom", default="VDPTEST.sms")
    ap.add_argument("--region", choices=("ntsc", "pal"), default="ntsc", help="SMS video mode to test")
    ap.add_argument("--input", default=None, help="input script path/name; default is ROM-stem_CRC.input, then ROM-stem.input/script")
    ap.add_argument("--frames", type=int, default=1200)
    ap.add_argument("--cadence", type=int, default=10)
    ap.add_argument("--update", action="store_true", help="print observed hashes instead of failing")
    args = ap.parse_args(argv)

    binary = args.binary.resolve()
    rom_dir = args.rom_dir.resolve()
    rom = resolve_rom(rom_dir, args.rom)
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    script, checked_scripts = resolve_input_script(rom_dir, rom, args.input)
    missing = [str(p) for p in (binary, rom) if not p.exists()]
    if script is None:
        missing.append("input script (tried: " + ", ".join(checked_scripts) + ")")
    if missing:
        print("Missing file(s): " + ", ".join(missing), file=sys.stderr)
        return 2
    assert script is not None

    hashes = PAL_HASHES if args.region == "pal" else NTSC_HASHES

    if args.frames < max(hashes):
        print(f"--frames must be at least {max(hashes)} for the configured screenshot baselines", file=sys.stderr)
        return 2
    if args.cadence <= 0:
        print("--cadence must be positive", file=sys.stderr)
        return 2

    uncovered = [f for f in hashes if f % args.cadence != 0]
    if uncovered:
        print("--cadence does not capture configured baseline frame(s): " + ", ".join(map(str, sorted(uncovered))), file=sys.stderr)
        return 2

    prefix_name = f"vdptest_sms_{args.region}"
    prefix = out_dir / prefix_name
    wav = out_dir / f"{prefix_name}.wav"
    remove_stale_screenshots(out_dir, prefix_name)
    cmd = [
        str(binary), "--console", "sms", "--region", args.region, "--frames", str(args.frames),
        "--input-playback", str(script),
        "--screenshot-prefix", str(prefix), "--screenshot-every", str(args.cadence),
        "--audio-wav", str(wav), "--quiet", str(rom),
    ]
    subprocess.run(cmd, check=True)

    failures: list[str] = []
    print(f"VDPTEST SMS {args.region.upper()} screenshot regression")
    print(f"rom={rom.name} input={script.name}")
    print("%-8s %-8s %s" % ("frame", "status", "sha256"))
    for frame, expected in sorted(hashes.items()):
        shot = out_dir / f"{prefix_name}_{frame:06d}.ppm"
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
