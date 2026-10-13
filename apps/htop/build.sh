#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source htop-3.5.3.tar.xz htop-3.5.3
source_dir="$output/src/htop-3.5.3"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os PKG_CONFIG=false \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --sysconfdir=/etc --disable-unicode --enable-static \
        --disable-sensors --disable-capabilities --disable-delayacct --disable-hwloc --disable-affinity \
        --disable-unwind > configure.log
    make -j"$jobs" > make.log 2>&1
)
finish_app "$source_dir/htop" htop
