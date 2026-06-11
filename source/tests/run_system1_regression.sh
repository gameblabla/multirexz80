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
    # Headless WAV output is deterministic 44-byte PCM WAV.  Hash only the PCM
    # payload so container RIFF sizes/timestamps cannot affect regressions.
    tail -c +45 "$1" | sha256sum | awk '{print $1}'
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
    seconds=$(awk -v s="$start" -v e="$end" 'BEGIN { printf "%.3f", e - s }')

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
blockgal|blockgal.zip|system1|723dc2ecc2f687188acd7771fa15c43daa9612b65bc2078ae2a2a6c501fafcf1|8f48c9b990c2b9795121beb0b3f6c2db2ffd1ff4561d8384b83a7450e5efe117
blockgalb|blockgalb.zip|system1|f268e9645b50088746cf8a3406394c151d5b51092daf5a5c89d385feba2da675|fd907f65c2903627e9ca4bc81e449b307d9e61334a4a2fdcfd24cd5cb6dee5fc
choplift|choplift.zip|system1|cab50edda78c539cc50229db9f0208c19e9a86be101cd14aafa30e88eb610b7b|8dd8f2f1555b97ec379e7977840b9c627c552ff250cb8a78b1051c32a4230671
flicky|flicky.zip|system1|1d079273592a9365c4f096cc201b6ff749fb7d0cf466f401b7cf0bc6acb39b94|04c70ed0b47b56cdc2263435922022fcfbecd5f240861a83e675abc6c0971669
gardia|gardia.zip|system1|5d183a8a1faf0441be54c39af35ef3e20ee06aebaa7d76fad7a2d8401040b5aa|967c9a781ac02ab1ba754bd0e22978777418f9858782c9c6f2834c1494f35af0
teddybb|teddybb.zip|system1|c65e2a00e1ce211724b6f9bbc2412f5ef7b51b6deaff8ed6bde19a9bb1108448|90be5f138064d59415d916d6c461d0d00c6ed465cfa5e6108249e3983418c158
EOF

if [ "$failures" -ne 0 ]; then
    echo
    echo "REGRESSION DETECTED: $failures failure(s)"
    exit 1
fi

echo
echo "All System 1 audio/video regressions passed."
