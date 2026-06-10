#!/usr/bin/env python3
"""Regression for Tables Have Turned's Gradient with Spotlight effect.

The demo changes the SMS nametable base register in the middle of active
scanlines.  The visible spotlight should therefore be sampled at tile-fetch
boundaries; a renderer that only samples R2 once per line/frame shows a static
gradient panel around the 28-second mark.
"""
from __future__ import annotations

import argparse
import hashlib
from pathlib import Path
import subprocess
import sys

EXPECTED = {
    1680: "174f711a35d8be62ea6e33fd3af837c83bcfffc443dfb97b6d16bf6c0866e2c1",
    1800: "e33980333b030fd0609d8dda8a522f5b223a838200436216fe05d5ed7f9ec7e9",
    1920: "a304c365e8a327efeed893a045583cebd6e84520883139fb162800970798d0ca",
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def ppm_rgb_payload(path: Path) -> bytes:
    data = path.read_bytes()
    if not data.startswith(b"P6\n"):
        raise ValueError(f"{path}: not a binary PPM")
    pos = 0
    for _ in range(3):
        pos = data.index(b"\n", pos) + 1
    return data[pos:]


def changed_pixels(a: bytes, b: bytes) -> int:
    if len(a) != len(b):
        raise ValueError("PPM payload sizes differ")
    return sum(1 for i in range(0, len(a), 3) if a[i:i+3] != b[i:i+3])


def spotlight_bbox(payload: bytes, width: int = 256, height: int = 192) -> tuple[int, int, int, int] | None:
    """Return a conservative bright-spotlight bounding box for the open field.

    The old regression only proved that the effect moved.  A bad fetch cadence
    still passed while compressing the spotlight horizontally.  Frame 1800 keeps
    the spot away from the title panels, so a simple bright-pixel mask is stable
    enough to catch that failure without matching a particular palette value.
    """
    xs: list[int] = []
    ys: list[int] = []
    for y in range(80, height):
        row = y * width * 3
        for x in range(width):
            i = row + x * 3
            r, g, b = payload[i], payload[i + 1], payload[i + 2]
            if r >= 180 and g >= 70 and b >= 90:
                xs.append(x)
                ys.append(y)
    if not xs:
        return None
    return min(xs), max(xs), min(ys), max(ys)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run Tables Have Turned spotlight regression.")
    ap.add_argument("--binary", type=Path, default=Path("./multirexz80_headless"))
    ap.add_argument("--rom", type=Path, default=Path("roms/tables_turned.sms"))
    ap.add_argument("--out-dir", type=Path, default=Path("test-results/tables_turned_spotlight"))
    ap.add_argument("--frames", type=int, default=1920)
    ap.add_argument("--console", default="sms2")
    ap.add_argument("--update", action="store_true", help="print observed hashes instead of failing")
    args = ap.parse_args(argv)

    binary = args.binary.resolve()
    rom = args.rom.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    if not binary.exists():
        raise SystemExit(f"headless binary not found: {binary}")
    if not rom.exists():
        raise SystemExit(f"ROM not found: {rom}")
    if args.frames < max(EXPECTED):
        raise SystemExit(f"--frames must be at least {max(EXPECTED)}")

    prefix = out_dir / "tables_turned"
    cmd = [
        str(binary), "--console", args.console, "--frames", str(args.frames),
        "--screenshot-prefix", str(prefix), "--screenshot-every", "120",
        "--quiet", str(rom),
    ]
    subprocess.run(cmd, check=True)

    failures: list[str] = []
    payloads: dict[int, bytes] = {}
    print("Tables Have Turned spotlight regression")
    print("%-8s %-8s %s" % ("frame", "status", "sha256"))
    for frame, expected in sorted(EXPECTED.items()):
        shot = out_dir / f"tables_turned_{frame:06d}.ppm"
        if not shot.exists():
            failures.append(f"frame {frame}: missing screenshot {shot}")
            print("%-8d %-8s %s" % (frame, "MISSING", shot.name))
            continue
        observed = sha256_file(shot)
        ok = args.update or observed == expected
        print("%-8d %-8s %s" % (frame, "ok" if ok else "FAIL", observed))
        if not ok:
            failures.append(f"frame {frame}: got {observed}, expected {expected}")
        payloads[frame] = ppm_rgb_payload(shot)

    if 1680 in payloads and 1800 in payloads and 1920 in payloads:
        d1 = changed_pixels(payloads[1680], payloads[1800])
        d2 = changed_pixels(payloads[1800], payloads[1920])
        print(f"motion_pixels 1680->1800={d1} 1800->1920={d2}")
        if not args.update and (d1 < 500 or d2 < 500):
            failures.append("spotlight frames are too similar; the effect is likely static")

    if 1800 in payloads:
        bbox = spotlight_bbox(payloads[1800])
        if bbox is None:
            failures.append("frame 1800: could not locate bright spotlight")
            print("spotlight_bbox frame=1800 MISSING")
        else:
            x0, x1, y0, y1 = bbox
            w = x1 - x0 + 1
            h = y1 - y0 + 1
            print(f"spotlight_bbox frame=1800 x={x0}..{x1} y={y0}..{y1} size={w}x{h}")
            if not args.update and w < 56:
                failures.append(f"frame 1800: spotlight is too skinny horizontally ({w}px wide)")
            if not args.update and h < 48:
                failures.append(f"frame 1800: spotlight is too short vertically ({h}px high)")

    if args.update:
        print("\nObserved hashes above. Update EXPECTED after intentional VDP timing changes.")
        return 0
    if failures:
        print("\nTABLES HAVE TURNED REGRESSION DETECTED")
        for f in failures:
            print("  -", f)
        return 1
    print("\nTables Have Turned spotlight regression passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
