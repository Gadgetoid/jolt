#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../common.sh"

unpack_source mbedtls-3.6.7.tar.bz2 mbedtls-3.6.7
source_dir="$output/src/mbedtls-3.6.7"
make -C "$source_dir/library" -j"$jobs" CC="$APP_WRAPPER" AR="$llvm/bin/llvm-ar" \
    CFLAGS="-Os -ffunction-sections -fdata-sections" static > "$source_dir/make.log" 2>&1
cp "$source_dir"/library/libmbedtls.a "$source_dir"/library/libmbedx509.a "$source_dir"/library/libmbedcrypto.a "$sysroot/"
rm -rf "$sysroot/include/mbedtls" "$sysroot/include/psa"
cp -R "$source_dir/include/mbedtls" "$source_dir/include/psa" "$sysroot/include/"
