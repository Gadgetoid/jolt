#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source frotz-2.55.tar.gz frotz-2.55
source_dir="$output/src/frotz-2.55"
patch -d "$source_dir" -p1 < "$(dirname "$0")/no-root-check.patch" > /dev/null
make -C "$source_dir" -j"$jobs" nosound CC="$APP_WRAPPER" AR="$llvm/bin/llvm-ar" RANLIB="$llvm/bin/llvm-ranlib" \
    CFLAGS="-std=c99 -Os -D_DEFAULT_SOURCE" CURSES=ncurses NO_PKGCONF=yes NO_BLORB=yes UNAME_S=Linux USE_UTF8=no \
    SYSCONFDIR=/etc PREFIX=/usr > "$source_dir/make.log" 2>&1
finish_app "$source_dir/frotz" frotz

mkdir -p "$output/share/games/zork"
for game in zork1 zork2 zork3; do cp "$deps/archives/$game.z3" "$output/share/games/zork/"; done
cp "$deps/archives/zork-LICENSE" "$output/share/games/zork/LICENSE"
