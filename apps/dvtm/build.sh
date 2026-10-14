#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source dvtm-0.15.tar.gz dvtm-0.15
source_dir="$output/src/dvtm-0.15"
patch -d "$source_dir" -p1 < "$(dirname "$0")/narrow-curses.patch" > /dev/null
(
    cd "$source_dir"
    cp config.def.h config.h
    make dvtm CC="$APP_WRAPPER" LDFLAGS="-lncurses" \
        CFLAGS="-std=c99 -I. -DVERSION=\\\"0.15\\\" -DNDEBUG -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700" > make.log 2>&1
)
finish_app "$source_dir/dvtm" dvtm
