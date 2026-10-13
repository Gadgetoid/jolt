#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source jq-1.8.2.tar.gz jq-1.8.2
source_dir="$output/src/jq-1.8.2"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os PKG_CONFIG=false \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --disable-shared --enable-static --without-oniguruma \
        --disable-docs --disable-valgrind --disable-maintainer-mode > configure.log
    make -j"$jobs" > make.log 2>&1
)
finish_app "$source_dir/jq" jq
