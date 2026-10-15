#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
emulator=${JORNADA_EMU:-"$workspace/../jornada-72x-emu"}
state=${JORNADA_STATE:-"$emulator/build/user-latest.state"}
scripts=${JOLT_SCRIPTS:-"smoke shell interrupt vi spawn display font fullscreen-exit colour jobs apps lua network extras netapps qol desktop vim shortcut launch hold serial apps2 ping bash pty dvtm dvtm-resize serial-shell bitmap-font subst sfeed theme vim-typing colour-screen fonts-screen dvtm-screen bitmap-screen scroll-screen theme-screen sfeed-screen"}
batch_size=${JOLT_BATCH:-4}
socket="/tmp/jolt-qa-$$.sock"
serial_port=$((20000 + $$ % 10000))
peer_pid=
qa=build/qa
stage=$qa/card/JoLT
card=/Storage\ Card/JoLT
emulator_pid=

cd "$workspace"
if [[ ! -f build/dist/stage/JoLT/JoLT-Term.exe ]]; then
    printf '%s\n' 'Run tools/build.sh first' >&2
    exit 1
fi

stop_emulator() {
    if [[ -n $peer_pid ]]; then
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
    peer_pid=
    if [[ -n $emulator_pid ]]; then
        kill "$emulator_pid" 2>/dev/null || true
        wait "$emulator_pid" 2>/dev/null || true
    fi
    emulator_pid=
}
trap stop_emulator EXIT

start_emulator() {
    "$emulator/headless" --rom="$emulator/rom/jornada720.bin" \
        --eeprom="$emulator/rom/jorn720_eeprom.bin" --load="$state" \
        --card="$workspace/$qa/card.cf" --net=0 --rapi="$socket" --tcp-com4="$serial_port" \
        --seconds=1000000 --realtime=${JOLT_REALTIME:-4} ${JOLT_EMULATOR_FLAGS:-} --screenshot="$workspace/$qa/screen.png" \
        > "$qa/emulator.log" 2>&1 &
    emulator_pid=$!
    JOLT_SOCKET="$socket" JOLT_LOG="$qa/emulator.log" node tests/wait-desktop.js
    node tests/serial-peer.js "$serial_port" &
    peer_pid=$!
}

rapi() {
    "$emulator/jornada-rapi" --socket="$socket" --timeout=30 "$@"
}

wait_for() {
    node tests/wait-result.js "$emulator/jornada-rapi" "--socket=$socket" "$card/$1" "$2" 120000
}

check_no_processes() {
    local name=$1 executable
    for executable in JoLT-Term JoltWorker; do
        if ! rapi mv "$card/$executable.exe" "$card/$executable.idle"; then
            printf 'FAIL: %s still running after %s\n' "$executable" "$name" >&2
            return 1
        fi
        rapi mv "$card/$executable.idle" "$card/$executable.exe"
    done
    printf 'PASS: %s left no processes\n' "$name"
}

run_screen_script() {
    local name=$1
    wait_for JoltScreen.ready "$qa/$name.txt"
    cat "$qa/$name.txt"
    sleep 2
}

run_script() {
    local name=$1 status=0
    printf 'run %s at %s\n' "$name" "$(date +%T)"
    rapi rm "$card/Jolt.log" 2>/dev/null || true
    rapi rm "$card/stall.log" 2>/dev/null || true
    local launch=""
    if [[ -f tests/scripts/$name.args ]]; then launch=$(< "tests/scripts/$name.args"); fi
    rapi run "$card/JoLT-Term.exe" "--script=$name.txt --quiet ${JOLT_TRACE:-} $launch"
    if [[ $name == *-screen ]]; then
        run_screen_script "$name"
        return
    fi
    wait_for Jolt.log "$qa/$name.log" || status=$?
    rapi get "$card/JoltTty.log" "$qa/$name-tty.log" || true
    if [[ -n ${JOLT_TRACE:-} ]]; then
        local pid
        for pid in 1 2 3 4 5 6 7 8 9 10; do
            rapi get "$card/JoltTrace-$pid.log" "$qa/$name-trace-$pid.log" 2>/dev/null || break
        done
    fi
    if [[ $status != 0 ]]; then
        printf 'FAIL: %s produced no result\n' "$name" >&2
        return 1
    fi
    cat -v "$qa/$name.log"
    node tests/check.js "$name" "$qa/$name.log" "$qa/$name-tty.log"
    if [[ $name == vim-typing ]] && rapi get "$card/stall.log" "$qa/$name-stall.log" 2>/dev/null; then
        cat "$qa/$name-stall.log"
        printf 'FAIL: %s had keys that never reached the screen\n' "$name" >&2
        return 1
    fi
    sleep 3
    check_no_processes "$name"
}

rm -rf "$qa"
mkdir -p "$qa/card"
cp -R build/dist/stage/JoLT "$stage"
printf '%s\n' 'Welcome to the JoLT CF root.' > "$stage/root/welcome.txt"
cp tests/scripts/*.txt "$stage/"
if [[ -n ${JOLT_TERM_EXE:-} ]]; then cp "$JOLT_TERM_EXE" "$stage/JoLT-Term.exe"; fi
sh "$emulator/tools/mkcard.sh" "$qa/card.cf" 32 "$stage"

read -r -a names <<< "$scripts"
for ((first = 0; first < ${#names[@]}; first += batch_size)); do
    start_emulator
    for name in "${names[@]:first:batch_size}"; do run_script "$name"; done
    rapi info
    stop_emulator
    if [[ -f $qa/screen.png ]]; then mv "$qa/screen.png" "$qa/screen-$name.png"; fi
done
printf '%s\n' 'PASS: JoLT shell QA; CE responsive'
