#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source nano-9.2.tar.xz nano-9.2
source_dir="$output/src/nano-9.2"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os PKG_CONFIG=false \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --sysconfdir=/etc --disable-nls --disable-utf8 \
        --disable-libmagic --disable-speller --disable-browser --without-slang > configure.log
    make -j"$jobs" > make.log 2>&1
)
finish_app "$source_dir/src/nano" nano
