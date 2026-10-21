#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source links-2.30.tar.bz2 links-2.30
source_dir="$output/src/links-2.30"
patch -d "$source_dir" -p1 < "$(dirname "$0")/cache.patch" > /dev/null
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" ./configure --host=arm-linux-gnueabi --disable-graphics --without-x \
        --without-fb --without-directfb --without-svgalib --without-pmshell --without-atheos --without-gpm \
        --with-ssl --without-zlib --without-brotli --without-zstd --without-bzip2 --without-lzma --without-libevent \
        --without-ipv6 --without-libjpeg --without-libtiff --without-librsvg --without-libwebp --without-libavif \
        > configure.log
    make -j"$jobs" > make.log
)
finish_app "$source_dir/links" links
