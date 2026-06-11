#!/usr/bin/env python3
"""Smoke regressions for SNK Ikari/Psycho Soldier hardware ROM-set loading and LS-30 controls.

This intentionally checks hardware-level symptoms rather than game-specific hacks:
- Chopper/Legofair-class archives must boot to non-blank output.
- Victory Road/Dogosoken-class archives must accept coin/start and reach active video with the
  MAME-style split LS-30 input ports wired.
"""
import argparse
import hashlib
import os
import pathlib
import subprocess
import sys


def read_ppm(path):
    with open(path, 'rb') as f:
        magic = f.readline().strip()
        if magic != b'P6':
            raise ValueError(f'{path}: unsupported PPM magic {magic!r}')
        line = f.readline()
        while line.startswith(b'#'):
            line = f.readline()
        w, h = map(int, line.split())
        maxval = int(f.readline())
        if maxval != 255:
            raise ValueError(f'{path}: unsupported maxval {maxval}')
        data = f.read()
    if len(data) != w * h * 3:
        raise ValueError(f'{path}: truncated PPM data')
    return w, h, data


def image_stats(path):
    w, h, data = read_ppm(path)
    colors = len({data[i:i+3] for i in range(0, len(data), 3)})
    sha = hashlib.sha256(data).hexdigest()
    return w, h, colors, sha


def run_capture(binary, rom, frames, out, input_script=None, console='psychos'):
    cmd = [binary, '--console', console, '--frames', str(frames), '--screenshot', str(out)]
    if input_script:
        cmd += ['--input-playback', str(input_script)]
    cmd.append(str(rom))
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f'command failed ({proc.returncode}): {" ".join(cmd)}\n{proc.stdout}')
    if not out.exists() or out.stat().st_size == 0:
        raise RuntimeError(f'missing screenshot: {out}\n{proc.stdout}')
    return proc.stdout


def assert_not_blank(label, ppm, min_colors, allowed_sizes):
    w, h, colors, sha = image_stats(ppm)
    if allowed_sizes and (w, h) not in allowed_sizes:
        raise AssertionError(f'{label}: unexpected geometry {(w, h)}, expected one of {sorted(allowed_sizes)}')
    if colors < min_colors:
        raise AssertionError(f'{label}: suspiciously blank output ({colors} colors, sha256={sha})')
    return w, h, colors, sha


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--binary', default='./multirexz80_headless')
    ap.add_argument('--chopper-rom', required=True)
    ap.add_argument('--victroad-rom', required=True)
    ap.add_argument('--psychos-rom')
    ap.add_argument('--out-dir', default='test-results/snk_romset_controls')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args(argv)

    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    report = []

    chopper_ppm = out_dir / 'chopper_3600.ppm'
    log = run_capture(args.binary, args.chopper_rom, 3600, chopper_ppm)
    st = assert_not_blank('chopper', chopper_ppm, min_colors=24, allowed_sizes={(224, 384)})
    report.append(('chopper', st, log.strip().splitlines()[-1] if log.strip() else ''))

    # Victory Road starts and then exercises movement/fire. The regression is that c100/c200/c300
    # must use the Ikari/Victory Road/GWAR LS-30 layout, otherwise gameplay controls read wrong.
    input_path = out_dir / 'victroad_ls30.input'
    input_path.write_text('\n'.join([
        '60f:COIN1@30',
        '300f:START1@30',
        # Exercise the real split LS-30 layout: movement low nibble and absolute
        # rotary high nibble must be independent. These actions used to alias
        # to rotate-left/right and could not explicitly aim up/down.
        '360f:+RIGHT,+AIM_UP,+A',
        '660f:-RIGHT,-AIM_UP,-A',
        '720f:+DOWN,+AIM_RIGHT,+A',
        '1020f:-DOWN,-AIM_RIGHT,-A',
        '1080f:+ROTATE_RIGHT,+A',
        '1180f:-ROTATE_RIGHT,-A',
        '',
    ]))
    vict_ppm = out_dir / 'victroad_1200.ppm'
    log = run_capture(args.binary, args.victroad_rom, 1200, vict_ppm, input_script=input_path)
    st = assert_not_blank('victroad', vict_ppm, min_colors=32, allowed_sizes={(216, 288)})
    report.append(('victroad', st, log.strip().splitlines()[-1] if log.strip() else ''))

    if args.psychos_rom:
        psychos_ppm = out_dir / 'psychos_3600.ppm'
        log = run_capture(args.binary, args.psychos_rom, 3600, psychos_ppm)
        st = assert_not_blank('psychos', psychos_ppm, min_colors=32, allowed_sizes={(400, 224)})
        report.append(('psychos', st, log.strip().splitlines()[-1] if log.strip() else ''))

    lines = []
    for name, (w, h, colors, sha), lastlog in report:
        lines.append(f'{name}: pass geometry={w}x{h} colors={colors} sha256={sha}')
        if lastlog:
            lines.append(f'  {lastlog}')
    text = '\n'.join(lines) + '\n'
    (out_dir / 'report.txt').write_text(text)
    if not args.quiet:
        print(text, end='')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
