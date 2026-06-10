#!/usr/bin/env bash
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
MAKEFILE=${SYSTEM1_REGRESSION_MAKEFILE:-Makefile.headless}
ROM_DIR=${SYSTEM1_REGRESSION_ROM_DIR:-$ROOT/roms}
OUT_DIR=${SYSTEM1_REGRESSION_OUT_DIR:-$ROOT/test-results/system1_regression}
FRAMES=${SYSTEM1_REGRESSION_FRAMES:-3600}
BINARY=${SYSTEM1_REGRESSION_BINARY:-$ROOT/multirexz80_headless}
JOBS=${SYSTEM1_REGRESSION_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
TIMEOUT=${SYSTEM1_REGRESSION_TIMEOUT:-180}

if [ ! -f "$ROOT/$MAKEFILE" ]; then
    echo "Makefile not found: $ROOT/$MAKEFILE" >&2
    exit 2
fi
if [ ! -d "$ROM_DIR" ]; then
    echo "ROM directory not found: $ROM_DIR" >&2
    echo "Put blockgal.zip, choplift.zip, flicky.zip, gardia.zip, and teddybb.zip in $ROM_DIR" >&2
    exit 2
fi

if [ "${SYSTEM1_REGRESSION_NO_BUILD:-0}" != "1" ]; then
    echo "Building headless with $MAKEFILE"
    make -C "$ROOT" -f "$MAKEFILE" -j"$JOBS" || exit $?
fi
if [ ! -x "$BINARY" ]; then
    echo "Headless binary not found or not executable: $BINARY" >&2
    exit 2
fi
mkdir -p "$OUT_DIR"

pcm_sha256() {
    python3 -c "import hashlib, sys, wave; p=sys.argv[1]; w=wave.open(p, 'rb'); params=w.getparams(); pcm=w.readframes(w.getnframes()); w.close(); assert params.nchannels == 2 and params.sampwidth == 2 and params.framerate > 0 and params.nframes > 0, params; print(hashlib.sha256(pcm).hexdigest())" "$1"
}

run_with_timeout() {
    if command -v timeout >/dev/null 2>&1; then
        timeout "$TIMEOUT" "$@"
    else
        "$@"
    fi
}

failures=0
printf '\nSega System 1 regression, frames=%s\n' "$FRAMES"
printf '%-10s %-9s %-9s %-10s %s\n' game video audio seconds outputs

while IFS='|' read -r game rom console ppm_expected pcm_expected; do
    [ -n "$game" ] || continue
    rom_path="$ROM_DIR/$rom"
    ppm="$OUT_DIR/$game.ppm"
    wav="$OUT_DIR/$game.wav"
    if [ ! -f "$rom_path" ]; then
        echo "$game: missing ROM: $rom_path" >&2
        failures=$((failures + 1))
        continue
    fi

    start=$(date +%s.%N)
    if ! run_with_timeout "$BINARY" --console "$console" --frames "$FRAMES" --screenshot "$ppm" --audio-wav "$wav" --quiet "$rom_path" < /dev/null; then
        printf '%-10s %-9s %-9s %-10s %s\n' "$game" FAIL FAIL - "runner failed"
        failures=$((failures + 1))
        continue
    fi
    end=$(date +%s.%N)
    seconds=$(python3 -c "import sys; print(f'{float(sys.argv[2]) - float(sys.argv[1]):.3f}')" "$start" "$end")

    ppm_actual=$(sha256sum "$ppm" | awk '{print $1}')
    pcm_actual=$(pcm_sha256 "$wav") || pcm_actual=INVALID
    video_status=ok
    audio_status=ok
    if [ "$ppm_actual" != "$ppm_expected" ]; then
        video_status=FAIL
        failures=$((failures + 1))
        echo "$game: screenshot SHA-256 mismatch: got $ppm_actual expected $ppm_expected" >&2
    fi
    if [ "$pcm_actual" != "$pcm_expected" ]; then
        audio_status=FAIL
        failures=$((failures + 1))
        echo "$game: PCM SHA-256 mismatch: got $pcm_actual expected $pcm_expected" >&2
    fi
    printf '%-10s %-9s %-9s %-10s %s, %s\n' "$game" "$video_status" "$audio_status" "$seconds" "$(basename "$ppm")" "$(basename "$wav")"
done <<'EOF'
blockgal|blockgal.zip|system1|723dc2ecc2f687188acd7771fa15c43daa9612b65bc2078ae2a2a6c501fafcf1|91072cf751b4a71bcb9e3ee7b7de2c36c707a3752d69312b3b5fc72cd3b470a2
choplift|choplift.zip|system1|5c58d4bb482315b7a4aa4e6abf7865c8facd862259885c6d54926e6d4bcd7601|5b32b840a68666cad8ee454c56e180beb38986524d7f407c17f1645a0d791cc2
flicky|flicky.zip|system1|af2c76cd3bf642d7f141023df9187f810e153f6b5f7f50a6fe6f985921349daa|04c70ed0b47b56cdc2263435922022fcfbecd5f240861a83e675abc6c0971669
gardia|gardia.zip|system1|5d183a8a1faf0441be54c39af35ef3e20ee06aebaa7d76fad7a2d8401040b5aa|460723343cd5be08ca004c6fae269ba6038cd7225e1f5427665dbe080db69f36
teddybb|teddybb.zip|system1|c65e2a00e1ce211724b6f9bbc2412f5ef7b51b6deaff8ed6bde19a9bb1108448|6c466ee5b2f9ddaaa225bfede26e2fa041e2ed952b0de0f2db97216798931242
EOF

if [ "$failures" -ne 0 ]; then
    echo
    echo "REGRESSION DETECTED: $failures failure(s)"
    exit 1
fi

echo
echo "All System 1 audio/video regressions passed."
