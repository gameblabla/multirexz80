#!/usr/bin/env python3
"""Deterministic headless audio/video regression suite.

Default PC workflow from the project root:

    python3 source/tests/headless_regression.py

The script builds Makefile.headless, looks for ROMs in ./roms next to the
Makefile, runs each workload long enough to pass startup/self-check screens,
and compares both the final screenshot SHA-256 and the WAV PCM SHA-256.
"""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import time
import wave

GAMES: dict[str, dict[str, object]] = {
    "blockgal": {
        "roms": ["blockgal.zip"],
        "console": "system1",
        "frames": 3600,
        "screenshot_sha256": "723dc2ecc2f687188acd7771fa15c43daa9612b65bc2078ae2a2a6c501fafcf1",
        "pcm_sha256": "8f48c9b990c2b9795121beb0b3f6c2db2ffd1ff4561d8384b83a7450e5efe117",
    },
    "blockgalb": {
        "roms": ["blockgalb.zip"],
        "console": "system1",
        "frames": 3600,
        "screenshot_sha256": "f268e9645b50088746cf8a3406394c151d5b51092daf5a5c89d385feba2da675",
        "pcm_sha256": "fd907f65c2903627e9ca4bc81e449b307d9e61334a4a2fdcfd24cd5cb6dee5fc",
    },
    "choplift": {
        "roms": ["choplift.zip"],
        "console": "system1",
        "frames": 3600,
        "screenshot_sha256": "cab50edda78c539cc50229db9f0208c19e9a86be101cd14aafa30e88eb610b7b",
        "pcm_sha256": "8dd8f2f1555b97ec379e7977840b9c627c552ff250cb8a78b1051c32a4230671",
    },
    "flicky": {
        "roms": ["flicky.zip"],
        "console": "system1",
        "frames": 3600,
        "screenshot_sha256": "1d079273592a9365c4f096cc201b6ff749fb7d0cf466f401b7cf0bc6acb39b94",
        "pcm_sha256": "04c70ed0b47b56cdc2263435922022fcfbecd5f240861a83e675abc6c0971669",
    },
    "gardia": {
        "roms": ["gardia.zip"],
        "console": "system1",
        "frames": 3600,
        "screenshot_sha256": "5d183a8a1faf0441be54c39af35ef3e20ee06aebaa7d76fad7a2d8401040b5aa",
        "pcm_sha256": "967c9a781ac02ab1ba754bd0e22978777418f9858782c9c6f2834c1494f35af0",
    },
    "teddybb": {
        "roms": ["teddybb.zip"],
        "console": "system1",
        "frames": 3600,
        "screenshot_sha256": "c65e2a00e1ce211724b6f9bbc2412f5ef7b51b6deaff8ed6bde19a9bb1108448",
        "pcm_sha256": "90be5f138064d59415d916d6c461d0d00c6ed465cfa5e6108249e3983418c158",
    },
    "psychos": {
        "roms": ["psychos.zip"],
        "console": "psychos",
        "frames": 3600,
        "screenshot_sha256": "aced9ac000a96edeacd3f79f4741e42b0484577d97e442151a42130b4d355b8b",
        "pcm_sha256": "1882368a44c4b39b8f970977f70507087611d3b34693b0a7fff4d92c90311266",
    },
    "ikari": {
        "roms": ["ikari.zip"],
        "console": "psychos",
        "frames": 3600,
        "screenshot_sha256": "3b1c1ded9d06c75570bbe4f20a2045e64f33a2c0e831152167f386cdde1e767f",
        "pcm_sha256": "99bdaa4bc3da37a01f2c83e08bf7f65bc9a0d9bb72ff8d5075df3c9b0febfdef",
    },
    "athena": {
        "roms": ["athena.zip"],
        "console": "psychos",
        "frames": 3600,
        "screenshot_sha256": "138f01e21352858617609bdfce04520b3afe5e117c3ba3ba49bd2a3d1f09ebe7",
        "pcm_sha256": "9481a3f27fa7c98970dae92a7c92dddf8d2ae1d724461a459c5e1eb34851b96b",
    },
    "sonic1_sms": {
        "roms": ["Sonic 1 FM v1.02.sms", "sonic1fm.sms", "sonic1.sms"],
        "console": "sms",
        "frames": 3600,
        "screenshot_sha256": "eb3c62b5e276f4876d6cb05323e34b3816c06f785bda22959300c6d4e7a1c431",
        "pcm_sha256": "4134d175dec6721e808d390291f30b3c5d67265ea276af3cb6dd3e63d1829faf",
    },

    "sonic_drift2_gg": {
        "roms": ["drif2.gg", "Sonic Drift 2 (World).gg", "sonic_drift2.gg"],
        "console": "gg",
        "frames": 3600,
        "screenshot_sha256": "d2f031809d7cd79621fb57afe5cd1d493a9f8a264b3c03831db88c152517a23e",
        "pcm_sha256": "c7ef6074442c8f70b02ff28df1b874d47df21f8b46ddc9ed60077f70ea483750",
    },

    "tetrisse": {
        "roms": ["tetrisse.zip"],
        "console": "systeme",
        "frames": 3600,
        "screenshot_sha256": "802306d10d1479e5634ff9d1a599983b74817de6a50eaadf91c77e5ea5b60e15",
        "pcm_sha256": "ea5a46641af14e84d84a0fe19a5236a098ea8f9354a00a88a310dda9b613ff94",
    },
    "transfrm": {
        "roms": ["transfrm.zip"],
        "console": "systeme",
        "frames": 3600,
        "screenshot_sha256": "68c7852c22590c31b4f26db6344b429f3f3ee0a975296ffb0e1979ae59afcff5",
        "pcm_sha256": "49722a787982c00a7a639fc1e9b4f87707bbdc6c83cb60c8b3d93001b6283035",
    },
    "drift2": {
        # The user's ROM bundle contains drift2.zip as a zipped Game Gear ROM,
        # not a Sega System E arcade set.  Do not force it through the System E
        # path: that produced a deterministic black frame and allowed a false
        # rendering pass.  Auto/gg loads both drif2.gg and the zipped .gg member.
        "roms": ["drif2.gg", "drift2.zip", "Sonic Drift 2 (World).gg", "sonic_drift2.gg"],
        "console": "auto",
        "frames": 3600,
        "screenshot_sha256": "d2f031809d7cd79621fb57afe5cd1d493a9f8a264b3c03831db88c152517a23e",
        "pcm_sha256": "c7ef6074442c8f70b02ff28df1b874d47df21f8b46ddc9ed60077f70ea483750",
    },

    "fantastic_dizzy_sms_logo_wave_250": {
        "roms": ["Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms", "fantastic_dizzy.sms"],
        "input_scripts": ["Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1.input", "fantastic_dizzy.input"],
        "console": "auto",
        "frames": 250,
        "screenshot_sha256": "e75b62370f6c43318588510b57174edff822f526a27fec24df5ce863f29ffc89",
        "pcm_sha256": "78ec032ad11e651d2bbea8a06495ff91101477d094c4a4fa41856f7bf73b5811",
    },

    "fantastic_dizzy_sms_logo_wave_300": {
        "roms": ["Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms", "fantastic_dizzy.sms"],
        "input_scripts": ["Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1.input", "fantastic_dizzy.input"],
        "console": "auto",
        "frames": 300,
        "screenshot_sha256": "80dd0c4f8ba8fb84043fdb1c4f8fd32d8333413e90f123f664097ecdc9863245",
        "pcm_sha256": "8f6e2a410d794ec6c698e8fdf0b2644f0c28f0ed460f70d5cf12fbf12d800411",
    },

    "fantastic_dizzy_sms_logo_wave_350": {
        "roms": ["Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms", "fantastic_dizzy.sms"],
        "input_scripts": ["Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1.input", "fantastic_dizzy.input"],
        "console": "auto",
        "frames": 350,
        "screenshot_sha256": "7e8b23c945d7e7bb9ae63ad315035d190445d245e44d2eee4b670e1b6381c952",
        "pcm_sha256": "d07adc053c03d4e9e42acb8f4fe0dedea30c2dd304ec2901d2b7872c832d230b",
    },

    "fantastic_dizzy_sms_hud": {
        "roms": ["Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms", "fantastic_dizzy.sms"],
        "input_scripts": ["Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1.input", "fantastic_dizzy.input"],
        "console": "auto",
        "frames": 995,
        "screenshot_sha256": "47bd5cf8ef8e7b1d74ba150e60fc18a4055b05ade623750ce31ea6b25846f9f0",
        "pcm_sha256": "7423d7730d1a8ef7e54c1a01b25ca9089a36703e907d57e419f27e0aa5d107a4",
    },

    "fantastic_dizzy_sms_language_logo": {
        "roms": ["Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms", "fantastic_dizzy.sms"],
        "input_scripts": ["Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1.input", "fantastic_dizzy.input"],
        "console": "auto",
        "frames": 600,
        "screenshot_sha256": "09949dad817b002f1eb7cab620ca8d11cbe2812d5efcfef889e24c7de7a32727",
        "pcm_sha256": "e305f5c7d768ddd60a86ed19ffe2de80934537bbb96f4cc1cec31d9b5a6eab81",
    },

    "fantastic_dizzy_sms_paper_scroll": {
        "roms": ["Fantastic Dizzy (Europe) (En,Fr,De,Es,It).sms", "fantastic_dizzy.sms"],
        "input_scripts": ["Fantastic_Dizzy__Europe___En_Fr_De_Es_It__B9664AE1_glitchy_menu.input", "fantastic_dizzy_glitchy_menu.input"],
        "console": "auto",
        "frames": 700,
        "screenshot_sha256": "81f8fbdcd01016b3a195d1445cdb01d28418a5b956e7ee26f90b0e021e0a0a6e",
        "pcm_sha256": "f8244db3c7691c70c4c7d8ceca4a6d0472f96924743e326622bf928e2f0e6a05",
    },


    "madou_gg_vdp_latch": {
        "roms": ["Madou Monogatari I - 3tsu no Madoukyuu (Japan).gg", "madou.gg"],
        "input_scripts": ["Madou_Monogatari_I_-_3tsu_no_Madoukyuu__Japan__00C34D94.input", "madou.input"],
        "console": "gg",
        "frames": 3800,
        "screenshot_sha256": "190943688e882f8326730c0ba587cd3a8791f3c7ba531393f0e482d34ef68a72",
        "pcm_sha256": "e363cdba2b02c88222dac7c7961c108e97ef5e5821e741f7ea21c7a39cb3167a",
    },

    "tarzan_gg": {
        "roms": ["Tarzan - Lord of the Jungle (Europe).gg", "tarzan.gg"],
        "input_scripts": ["tarzan.script", "tarzan.input"],
        "console": "gg",
        "frames": 4000,
        "screenshot_sha256": "35764a277d819839d68caa7520a36e61a2b93dfa82f5b963a69e8b1bbaba2ef5",
        "pcm_sha256": "35f591b41f3b8164d41e405533234af1033430134ad4d358c0af9bae0f53ab74",
    },

    "tarzan_gg_hud": {
        "roms": ["Tarzan - Lord of the Jungle (Europe).gg", "tarzan.gg"],
        "input_scripts": ["tarzan.script", "tarzan.input"],
        "console": "gg",
        "frames": 900,
        "screenshot_sha256": "f4e1e32e32add3c040e1f8762ab03758b8c98b05df3ce5f293b05cc6eb5075cc",
        "pcm_sha256": "d92837c19b6a69f39fb3ecc868651bf815ff58ef0c61b31dd8b8b523e9bf753e",
    },
}

DEFAULT_GAMES = ["blockgal", "blockgalb", "choplift", "flicky", "gardia", "teddybb", "psychos", "ikari", "athena", "tetrisse", "transfrm", "drift2", "sonic1_sms", "sonic_drift2_gg", "fantastic_dizzy_sms_logo_wave_250", "fantastic_dizzy_sms_logo_wave_300", "fantastic_dizzy_sms_logo_wave_350", "fantastic_dizzy_sms_hud", "fantastic_dizzy_sms_language_logo", "madou_gg_vdp_latch", "tarzan_gg", "tarzan_gg_hud"]


def project_root_from_script() -> Path:
    return Path(__file__).resolve().parents[2]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def pcm_sha256_wav(path: Path) -> tuple[str, str]:
    with wave.open(str(path), "rb") as w:
        params = w.getparams()
        pcm = w.readframes(w.getnframes())
    if params.nchannels != 2:
        raise AssertionError(f"{path}: expected stereo WAV, got {params.nchannels} channel(s)")
    if params.sampwidth != 2:
        raise AssertionError(f"{path}: expected 16-bit WAV, got {params.sampwidth * 8}-bit")
    if params.framerate <= 0 or params.nframes <= 0:
        raise AssertionError(f"{path}: WAV has no audio frames")
    return hashlib.sha256(pcm).hexdigest(), f"{params.nframes}f/{params.framerate}Hz"


def run_checked(cmd: list[str], cwd: Path, timeout: float | None = None) -> None:
    proc = subprocess.run(cmd, cwd=str(cwd), timeout=timeout)
    if proc.returncode != 0:
        raise RuntimeError("command failed with status %d: %s" % (proc.returncode, " ".join(cmd)))


def build_headless(root: Path, makefile: str, jobs: int, clang_audit: bool) -> None:
    cmd = ["make", "-f", makefile, f"-j{jobs}"]
    if clang_audit:
        cmd += ["CC=clang", "CLANG_AUDIT=1"]
    print("Building headless:", " ".join(cmd), flush=True)
    run_checked(cmd, root)


def find_named_file(rom_dir: Path, names: list[str]) -> Path | None:
    for name in names:
        p = rom_dir / name
        if p.exists():
            return p
    return None


def main(argv: list[str]) -> int:
    root_default = project_root_from_script()
    ap = argparse.ArgumentParser(description="Build and run deterministic headless audio/video regressions.")
    ap.add_argument("--root", type=Path, default=root_default, help="project root; defaults to the directory containing Makefile.headless")
    ap.add_argument("--makefile", default="Makefile.headless", help="headless Makefile name")
    ap.add_argument("--binary", default="./multirexz80_headless", help="headless binary path, relative to --root unless absolute")
    ap.add_argument("--rom-dir", type=Path, default=None, help="ROM directory; defaults to ./roms next to the Makefile")
    ap.add_argument("--out-dir", type=Path, default=None, help="output directory; defaults to ./test-results/headless_regression")
    ap.add_argument("--frames", type=int, default=None, help="override frames for every game")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2, help="make parallelism")
    ap.add_argument("--no-build", action="store_true", help="do not build before running")
    ap.add_argument("--clang-audit", action="store_true", help="build with clang and CLANG_AUDIT=1 before running")
    ap.add_argument("--games", nargs="+", choices=sorted(GAMES), default=DEFAULT_GAMES, help="subset of games to run")
    ap.add_argument("--timeout", type=float, default=240.0, help="per-game timeout in seconds")
    args = ap.parse_args(argv)

    root = args.root.resolve()
    rom_dir = (args.rom_dir if args.rom_dir is not None else root / "roms").resolve()
    out_dir = (args.out_dir if args.out_dir is not None else root / "test-results" / "headless_regression").resolve()
    binary = Path(args.binary)
    if not binary.is_absolute():
        binary = root / binary

    if args.frames is not None and args.frames <= 0:
        raise SystemExit("--frames must be positive")
    if not (root / args.makefile).exists():
        raise SystemExit(f"Makefile not found: {root / args.makefile}")
    if not rom_dir.is_dir():
        raise SystemExit(f"ROM directory not found: {rom_dir}")

    missing: list[str] = []
    resolved_roms: dict[str, Path] = {}
    resolved_inputs: dict[str, Path] = {}
    for name in args.games:
        rom = find_named_file(rom_dir, list(GAMES[name]["roms"]))
        if rom is None:
            missing.append("%s ROM (%s)" % (name, " or ".join(GAMES[name]["roms"])))
        else:
            resolved_roms[name] = rom
        input_names = list(GAMES[name].get("input_scripts", []))
        if input_names:
            script = find_named_file(rom_dir, input_names)
            if script is None:
                missing.append("%s input (%s)" % (name, " or ".join(input_names)))
            else:
                resolved_inputs[name] = script
    if missing:
        raise SystemExit("Missing regression file(s) in %s: %s" % (rom_dir, ", ".join(missing)))

    if not args.no_build:
        build_headless(root, args.makefile, args.jobs, args.clang_audit)
    if not binary.exists():
        raise SystemExit(f"headless binary not found: {binary}")

    out_dir.mkdir(parents=True, exist_ok=True)
    failures: list[str] = []
    print("\nHeadless audio/video regression")
    print("%-12s %-9s %-9s %-7s %-10s %s" % ("game", "video", "audio", "frames", "seconds", "outputs"), flush=True)

    for name in args.games:
        spec = GAMES[name]
        frames = int(args.frames if args.frames is not None else spec["frames"])
        ppm = out_dir / f"{name}.ppm"
        wav = out_dir / f"{name}.wav"
        rom = resolved_roms[name]
        cmd = [str(binary), "--console", str(spec["console"]), "--frames", str(frames), "--screenshot", str(ppm), "--audio-wav", str(wav), "--quiet"]
        if name in resolved_inputs:
            cmd += ["--input-playback", str(resolved_inputs[name])]
        cmd.append(str(rom))
        start = time.perf_counter()
        try:
            run_checked(cmd, root, timeout=args.timeout)
            elapsed = time.perf_counter() - start
            video_sha = sha256_file(ppm)
            audio_sha, wav_desc = pcm_sha256_wav(wav)
            video_ok = video_sha == spec["screenshot_sha256"]
            audio_ok = audio_sha == spec["pcm_sha256"]
            if not video_ok:
                failures.append(f"{name}: screenshot SHA-256 mismatch: got {video_sha}, expected {spec['screenshot_sha256']}")
            if not audio_ok:
                failures.append(f"{name}: PCM SHA-256 mismatch: got {audio_sha}, expected {spec['pcm_sha256']}")
            print("%-12s %-9s %-9s %-7d %-10.3f %s, %s" % (name, "ok" if video_ok else "FAIL", "ok" if audio_ok else "FAIL", frames, elapsed, ppm.name, wav_desc), flush=True)
        except Exception as exc:
            failures.append(f"{name}: {exc}")
            print("%-12s %-9s %-9s %-7d %-10s %s" % (name, "FAIL", "FAIL", frames, "-", exc), flush=True)

    if failures:
        print("\nREGRESSION DETECTED")
        for f in failures:
            print("  -", f)
        return 1
    print("\nAll headless audio/video regressions passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
