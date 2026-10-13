#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../common.sh"

unpack_source libressl-4.3.3.tar.gz libressl-4.3.3
source_dir="$output/src/libressl-4.3.3"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS="-Os -ffunction-sections -fdata-sections" \
        ./configure --host=arm-linux-gnueabi --prefix="$sysroot" --includedir="$sysroot/include" --libdir="$sysroot" \
        --with-openssldir=/etc/ssl --disable-shared --enable-static --disable-asm --disable-tests > configure.log
    make -j"$jobs" -C crypto > make.log 2>&1
    make -j"$jobs" -C ssl >> make.log 2>&1
    make -C crypto install > install.log
    make -C ssl install >> install.log
    make -C include install >> install.log
)
