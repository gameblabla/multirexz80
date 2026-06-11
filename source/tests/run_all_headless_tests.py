#!/usr/bin/env python3
"""Run every Makefile.headless test target and report per-target failures.

This intentionally keeps going after failures so missing ROMs, missing save-state
fixtures, hash mismatches, and emulator crashes are all visible in one pass.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def parse_test_targets(makefile: Path) -> list[str]:
    targets: list[str] = []
    pat = re.compile(r"^(test-[A-Za-z0-9_.-]+)\s*:")
    for line in makefile.read_text(encoding="utf-8", errors="replace").splitlines():
        m = pat.match(line)
        if not m:
            continue
        target = m.group(1)
        if target == "test-all" or target in targets:
            continue
        targets.append(target)

    # zexall deliberately rebuilds with debug-console support; keep it last so it
    # cannot perturb the normal optimized binary used by the other tests.
    for special in ("test-zexall-debug-console",):
        if special in targets:
            targets.remove(special)
            targets.append(special)
    return targets


def find_bios(root: Path, rom_dir: Path) -> Path:
    for candidate in (rom_dir / "BIOS.col", root / "BIOS.col", Path("BIOS.col")):
        if candidate.exists():
            return candidate.resolve()
    return (rom_dir / "BIOS.col").resolve()


def make_vars(root: Path, rom_dir: Path, out_dir: Path, coleco_bios: Path) -> list[str]:
    r = str(rom_dir)
    o = str(out_dir)
    fd_rom = rom_dir / "Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms"
    fd_state = rom_dir / "Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1.auto.png"
    return [
        f"HEADLESS_REGRESSION_ROM_DIR={r}",
        f"HEADLESS_REGRESSION_OUT_DIR={o}/headless_regression",
        f"SYSTEM1_REGRESSION_ROM_DIR={r}",
        f"SYSTEM1_REGRESSION_OUT_DIR={o}/system1_regression",
        f"MADOUGG_ROM_DIR={r}",
        f"MADOUGG_OUT_DIR={o}/madou_vdp_latch",
        f"FANTASTIC_DIZZY_ROM_DIR={r}",
        f"FANTASTIC_DIZZY_OUT_DIR={o}/fantastic_dizzy_sms_hud",
        f"FANTASTIC_DIZZY_LANGUAGE_OUT_DIR={o}/fantastic_dizzy_sms_language_logo",
        f"FANTASTIC_DIZZY_PAPER_SCROLL_OUT_DIR={o}/fantastic_dizzy_sms_paper_scroll",
        f"FANTASTIC_DIZZY_LOGO_WAVE_OUT_DIR={o}/fantastic_dizzy_sms_logo_wave",
        f"FANTASTIC_DIZZY_REGRESSION_OUT_DIR={o}/fantastic_dizzy_sms_regression",
        f"FANTASTIC_DIZZY_STATE_OUT_DIR={o}/fantastic_dizzy_sms_state_right_hud",
        f"FANTASTIC_DIZZY_STATE_ROM={fd_rom}",
        f"FANTASTIC_DIZZY_STATE={fd_state}",
        f"ARCADE_REGRESSION_ROM_DIR={r}",
        f"ARCADE_REGRESSION_OUT_DIR={o}/arcade_regression",
        f"IKARI_TITLE_ROM={rom_dir / 'ikari.zip'}",
        f"IKARI_TITLE_OUT_DIR={o}/ikari_title",
        f"ATHENA_ATTRACT_ROM={rom_dir / 'athena.zip'}",
        f"ATHENA_ATTRACT_OUT_DIR={o}/athena_attract",
        f"CHOPLIFT_BOOT_ROM={rom_dir / 'choplift.zip'}",
        f"CHOPLIFT_BOOT_OUT_DIR={o}/choplift_boot",
        f"FLICKY_GAMEPLAY_ROM={rom_dir / 'flicky.zip'}",
        f"FLICKY_GAMEPLAY_OUT_DIR={o}/flicky_gameplay",
        f"BTTF3_PAL_ROM={rom_dir / 'Back to the Future Part III (Europe).sms'}",
        f"BTTF3_PAL_OUT_DIR={o}/bttf3_pal_start",
        f"SONIC_DRIFT2_GG_ROM_DIR={r}",
        f"SONIC_DRIFT2_GG_OUT_DIR={o}/sonic_drift2_gg",
        f"TARZAN_GG_ROM_DIR={r}",
        f"TARZAN_GG_OUT_DIR={o}/tarzan_gg_hud",
        f"SMS_VDP_LATCH_ROM_DIR={r}",
        f"SMS_VDP_LATCH_OUT_DIR={o}/sms_vdp_latch",
        f"SMS_VDP_LATCH_ROM={rom_dir / 'Madou Monogatari I - 3tsu no Madoukyuu (Japan).gg'}",
        f"VDPTEST_ROM_DIR={r}",
        f"VDPTEST_OUT_DIR={o}/vdptest_sms",
        f"VDPTEST_PAL_OUT_DIR={o}/vdptest_sms_pal",
        f"COLECO_VM_MULTICOLOR_ROM={rom_dir / 'CV' / 'vm_multicolor.cv'}",
        f"COLECO_VM_MULTICOLOR_BIOS={coleco_bios}",
        f"COLECO_VM_MULTICOLOR_OUT_DIR={o}/coleco_vm_multicolor",
        f"COLECO_MULTI_SPRITES_ROM={rom_dir / 'CV' / 'multi_sprites.cv'}",
        f"COLECO_MULTI_SPRITES_BIOS={coleco_bios}",
        f"COLECO_MULTI_SPRITES_OUT_DIR={o}/coleco_multi_sprites",
        f"PSYCHOS_SPRITE_ROM={rom_dir / 'psychos.zip'}",
        f"PSYCHOS_SPRITE_OUT_DIR={o}/psychos_sprite",
        f"PSYCHOS_ROM={rom_dir / 'psychos.zip'}",
        f"PSYCHOS_AUDIO_WAV={out_dir / 'psychos_audio.wav'}",
        f"BLOCKGAL_ROM={rom_dir / 'blockgal.zip'}",
        f"BLOCKGAL_SCREENSHOT={out_dir / 'blockgal.ppm'}",
        f"ZEXALL_ROM={rom_dir / 'zexall.sms'}",
        f"ZEXALL_DEBUG_LOG={out_dir / 'zexall_debug_console.log'}",
        f"TABLES_TURNED_ROM={rom_dir / 'tables_turned.sms'}",
        f"TABLES_TURNED_OUT_DIR={o}/tables_turned_spotlight",
    ]


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Run every Makefile.headless test target and summarize failures.")
    ap.add_argument("--root", type=Path, default=Path.cwd(), help="project root; default is current directory")
    ap.add_argument("--makefile", default="Makefile.headless")
    ap.add_argument("--rom-dir", type=Path, default=None, help="ROM directory; default is ./roms relative to --root")
    ap.add_argument("--out-dir", type=Path, default=None, help="output root; default is ./test-results/all relative to --root")
    ap.add_argument("--timeout", type=float, default=300.0, help="per-target timeout in seconds")
    ap.add_argument("--coleco-bios", type=Path, default=None, help="ColecoVision BIOS path; default probes ./roms/BIOS.col then ./BIOS.col")
    ap.add_argument("--jobs", default=str(os.cpu_count() or 2), help="parallelism passed to recursive make where it builds")
    ap.add_argument("--targets", nargs="+", default=None, help="explicit test target list; default is every test-* target in the makefile")
    args = ap.parse_args(argv)

    root = args.root.resolve()
    makefile = root / args.makefile
    rom_dir = (args.rom_dir if args.rom_dir is not None else root / "roms").resolve()
    out_dir = (args.out_dir if args.out_dir is not None else root / "test-results" / "all").resolve()
    if not makefile.exists():
        raise SystemExit(f"Makefile not found: {makefile}")
    if not rom_dir.exists():
        print(f"ROM directory not found: {rom_dir}", file=sys.stderr)
        print("Continuing anyway so each test reports its own missing-file diagnostics.", file=sys.stderr)
    out_dir.mkdir(parents=True, exist_ok=True)

    targets = args.targets if args.targets is not None else parse_test_targets(makefile)
    if not targets:
        raise SystemExit(f"No test-* targets found in {makefile}")

    coleco_bios = args.coleco_bios.resolve() if args.coleco_bios is not None else find_bios(root, rom_dir)
    vars_ = make_vars(root, rom_dir, out_dir, coleco_bios)
    failures: list[tuple[str, int | str, float]] = []

    print(f"Running {len(targets)} headless test target(s)")
    print(f"ROM dir: {rom_dir}")
    print(f"Output dir: {out_dir}")
    print("%-42s %-8s %-10s" % ("target", "result", "seconds"), flush=True)

    for target in targets:
        cmd = ["make", "-f", args.makefile, f"-j{args.jobs}", target, *vars_]
        start = time.perf_counter()
        try:
            proc = subprocess.run(cmd, cwd=root, timeout=args.timeout)
            elapsed = time.perf_counter() - start
            if proc.returncode == 0:
                print("%-42s %-8s %-10.3f" % (target, "ok", elapsed), flush=True)
            else:
                failures.append((target, proc.returncode, elapsed))
                print("%-42s %-8s %-10.3f" % (target, "FAIL", elapsed), flush=True)
        except subprocess.TimeoutExpired:
            elapsed = time.perf_counter() - start
            failures.append((target, "timeout", elapsed))
            print("%-42s %-8s %-10.3f" % (target, "TIMEOUT", elapsed), flush=True)

    print("\nSummary")
    if failures:
        for target, status, elapsed in failures:
            print(f"  FAIL {target}: {status} after {elapsed:.3f}s")
        print(f"{len(failures)} of {len(targets)} target(s) failed.")
        return 1

    print(f"All {len(targets)} target(s) passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
