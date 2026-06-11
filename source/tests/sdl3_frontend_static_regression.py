#!/usr/bin/env python3
"""Static sanity checks for the SDL3 frontend presentation path.

This catches the class of bug where arcade vertical games render correctly in
headless/Win32 but the SDL3 frontend allocates a host bitmap that is too short
for the active 224x400 SNK/GWAR-class visible area. It also verifies that the
frontend has an exact framebuffer PNG screenshot path for future comparisons.
"""
from __future__ import annotations
import argparse, re
from pathlib import Path

def require(cond: bool, msg: str, failures: list[str]) -> None:
    if not cond: failures.append(msg)

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', default='source/ports/sdl3/multirexz80_sdl3.cpp')
    ap.add_argument('--quiet', action='store_true')
    args = ap.parse_args()
    src_path = Path(args.source)
    text = src_path.read_text()
    failures: list[str] = []
    m_w = re.search(r'static\s+constexpr\s+int\s+BITMAP_W\s*=\s*(\d+)\s*;', text)
    m_h = re.search(r'static\s+constexpr\s+int\s+BITMAP_H\s*=\s*(\d+)\s*;', text)
    require(m_w is not None, 'missing BITMAP_W constant', failures)
    require(m_h is not None, 'missing BITMAP_H constant', failures)
    if m_w and m_h:
        w, h = int(m_w.group(1)), int(m_h.group(1))
        require(w >= 400, f'SDL3 bitmap width {w} is too small for 400x224 SNK horizontal frames', failures)
        require(h >= 400, f'SDL3 bitmap height {h} is too small for 224x400 SNK/GWAR vertical frames', failures)
    require('ACT_SCREENSHOT' in text, 'missing SDL3 screenshot action', failures)
    require('SDL_SCANCODE_F12' in text, 'missing F12 screenshot binding', failures)
    require('save_screenshot_png' in text, 'missing PNG screenshot function', failures)
    require('tdefl_write_image_to_png_file_in_memory' in text, 'screenshot path is not using the PNG encoder', failures)
    require('capture_active_preview' in text, 'screenshot path is not based on the active core viewport', failures)
    if failures:
        for f in failures: print(f'FAIL: {f}')
        return 1
    if not args.quiet: print(f'SDL3 frontend static regression: ok ({src_path})')
    return 0
if __name__ == '__main__': raise SystemExit(main())
