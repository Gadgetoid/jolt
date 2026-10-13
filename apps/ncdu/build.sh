#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source ncdu-1.22.tar.gz ncdu-1.22
source_dir="$output/src/ncdu-1.22"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os NCURSES_CFLAGS=-I. NCURSES_LIBS=-lncurses \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --with-ncurses > configure.log
    make -j"$jobs" > make.log 2>&1
)
finish_app "$source_dir/ncdu" ncdu
