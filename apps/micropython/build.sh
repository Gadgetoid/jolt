#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

recipe=$(cd "$(dirname "$0")" && pwd)
unpack_source micropython-1.29.0.tar.gz micropython-1.29.0
source_dir="$output/src/micropython-1.29.0"
patch -d "$source_dir" -p1 < "$recipe/blocking-delay.patch" > /dev/null
mkdir -p "$source_dir/lib/micropython-lib"
touch "$source_dir/lib/micropython-lib/README.md"
make -C "$source_dir/mpy-cross" -j"$jobs" CC=cc > "$source_dir/mpy-cross.log" 2>&1
make -C "$source_dir/ports/unix" -j"$jobs" VARIANT=standard UNAME_S=Linux CC="$APP_WRAPPER" LD="$APP_WRAPPER" \
    STRIP=true SIZE="$llvm/bin/llvm-size" \
    MICROPY_PY_THREAD=0 MICROPY_PY_FFI=0 MICROPY_SSL_MBEDTLS=0 MICROPY_SSL_AXTLS=0 MICROPY_PY_BTREE=0 \
    MICROPY_USE_READLINE=1 MICROPY_STANDALONE=1 FROZEN_MANIFEST="$recipe/manifest.py" \
    CFLAGS_EXTRA="-DMICROPY_NLR_SETJMP=1 -DMICROPY_GCREGS_SETJMP=1 -DMICROPY_PY_CRYPTOLIB=0 -DMICROPY_PY_HASHLIB_SHA1=0 -DMICROPY_PY_HASHLIB_MD5=0 -DMICROPY_EMIT_ARM=0 -DMICROPY_EMIT_THUMB=0 -DMICROPY_EMIT_INLINE_THUMB=0" \
    > "$source_dir/make.log" 2>&1
finish_app "$source_dir/ports/unix/build-standard/micropython" micropython
