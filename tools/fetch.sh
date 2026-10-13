#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
deps=${JOLT_DEPS:-"$workspace/build/deps"}
mkdir -p "$deps/archives"

fetch() {
    local name=$1 url=$2 checksum=$3
    local archive="$deps/archives/$name"
    if [[ ! -f "$archive" ]]; then curl -fL --retry 3 --max-time 300 -o "$archive.partial" "$url" && mv "$archive.partial" "$archive"; fi
    local actual
    actual=$(shasum -a 256 "$archive")
    if [[ ${actual%% *} != "$checksum" ]]; then
        printf 'Refused %s: checksum %s\n' "$name" "${actual%% *}" >&2
        return 1
    fi
}

unpack() {
    local name=$1 directory=$2
    if [[ ! -d "$deps/$directory" ]]; then tar -xf "$deps/archives/$name" -C "$deps"; fi
}

fetch musl-1.2.5.tar.gz https://musl.libc.org/releases/musl-1.2.5.tar.gz \
    a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
fetch busybox-1.38.0.tar.bz2 https://busybox.net/downloads/busybox-1.38.0.tar.bz2 \
    34f9ea6ff8636f2c9241153b9114eefa9e65674a45318ae1ef95bb5f31c53bb2
fetch libvterm-0.3.3.tar.gz https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz \
    09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0
fetch compiler-rt-21.1.8.src.tar.xz \
    https://github.com/llvm/llvm-project/releases/download/llvmorg-21.1.8/compiler-rt-21.1.8.src.tar.xz \
    dd54ae21aee1780fac59445b51ebff601ad016b31ac3a7de3b21126fd3ccb229
fetch lua-5.4.7.tar.gz https://www.lua.org/ftp/lua-5.4.7.tar.gz \
    9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30
fetch dropbear-2026.94.tar.bz2 https://matt.ucc.asn.au/dropbear/releases/dropbear-2026.94.tar.bz2 \
    e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81
fetch links-2.30.tar.bz2 http://links.twibright.com/download/links-2.30.tar.bz2 \
    c4631c6b5a11527cdc3cb7872fc23b7f2b25c2b021d596be410dadb40315f166
fetch ncurses-6.6.tar.gz https://ftp.gnu.org/gnu/ncurses/ncurses-6.6.tar.gz \
    355b4cbbed880b0381a04c46617b7656e362585d52e9cf84a67e2009b749ff11
fetch vim-9.2.1178.tar.gz https://github.com/vim/vim/archive/refs/tags/v9.2.1178.tar.gz \
    62b4a204676f0e2d6c11f1951ac204b1c83d22cd3f383599e7455e96cf87a68a
crypto=https://raw.githubusercontent.com/B-Con/crypto-algorithms/cfbde48414baacf51fc7c74f275190881f037d32
fetch sha256.c "$crypto/sha256.c" 4cbc93d30c28cee2a056d6f52cbce6e66ff9450f1d712ee4f59761b9b4486fad
fetch sha256.h "$crypto/sha256.h" a946e621775fd8251624e77d19f25407c4b6074ab780027e5797b77c7acf79b5

unpack libvterm-0.3.3.tar.gz libvterm-0.3.3
unpack compiler-rt-21.1.8.src.tar.xz compiler-rt-21.1.8.src
mkdir -p "$deps/crypto-algorithms"
cp "$deps/archives/sha256.c" "$deps/archives/sha256.h" "$deps/crypto-algorithms/"
printf '%s\n' "Dependencies ready in $deps"
