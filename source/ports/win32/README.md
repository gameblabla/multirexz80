# MultiRexZ80 native Win32 frontend

This frontend is intentionally separate from the SDL3 frontend.  It uses plain
Win32/GDI/waveOut and is cross-built with MinGW-w64 by `Makefile.win`.

Build both executables from Linux:

```sh
make -f Makefile.win
```

Outputs:

- `MultiRexZ80-win64.exe`, built with `x86_64-w64-mingw32-gcc`
- `MultiRexZ80-win32.exe`, built with `i686-w64-mingw32-gcc`

The makefile accepts normal overrides, for example:

```sh
make -f Makefile.win CC64=/opt/mingw64/bin/x86_64-w64-mingw32-gcc \
                    CC32=/opt/mingw32/bin/i686-w64-mingw32-gcc
```

Implemented frontend features:

- ROM/ZIP loading with the same core loader and CRC database as SDL3/headless.
- Command-line ROM loading, plus `--console`, `--region`/`--video-mode`,
  `--bios`, `--coleco-bios`, and `--m5-bios`.
- Machine menu with Auto, SMS1 JP, SMS1 export, SMS2, GG, GG SMS mode,
  SG-1000, ColecoVision, Sord M5, System E, Sega System 1/2, and SNK.
- Video mode menu: Auto, PAL 50 Hz, NTSC 60 Hz.
- BIOS paths window for Master System, ColecoVision, and Sord M5.  Paths are
  stored in `MultiRexZ80.ini` next to the executable and may be cleared.
- Configurable gameplay controls and configurable hotkeys.
- Pause, fast forward, rewind, save/load slots, screenshot, and fullscreen.
- Input recording and input playback using the existing `.input` script format.
- Light Phaser mouse capture only when the loaded ROM database selects a lightgun
  device. Click the window to grab, release the mouse button to release.

The Win32 port does not replace the SDL3 frontend.  SDL3 remains the Linux/AppImage
frontend and `Makefile.sdl3 package` remains the Linux packaging path.
