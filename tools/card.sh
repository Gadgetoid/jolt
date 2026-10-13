#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
emulator=${JORNADA_EMU:-"$workspace/../jornada-72x-emu"}
size=${JOLT_CARD_MB:-32}
cd "$workspace"
if [[ ! -f build/dist/stage/JoLT/JoLT-Term.exe ]]; then
    printf '%s\n' 'Run tools/build.sh first' >&2
    exit 1
fi
card=${1:-build/jolt-card.cf}
sh "$emulator/tools/mkcard.sh" "$card.partial" "$size" build/dist/stage/JoLT
mv "$card.partial" "$card"
printf '%s\n' "$card"
