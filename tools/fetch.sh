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
    local name=$1 directory=$2 patch_file=${3:-}
    if [[ -d "$deps/$directory" ]]; then return; fi
    tar -xf "$deps/archives/$name" -C "$deps"
    if [[ -n $patch_file ]]; then patch -d "$deps/$directory" -p1 < "$workspace/$patch_file" > /dev/null; fi
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
fetch nano-9.2.tar.xz https://ftp.gnu.org/gnu/nano/nano-9.2.tar.xz \
    05ecb99247b782e8a5b3a25ed4101dd034b0236902f7449bc9795b717642f7e9
fetch bash-5.3.tar.gz https://ftp.gnu.org/gnu/bash/bash-5.3.tar.gz \
    0d5cd86965f869a26cf64f4b71be7b96f90a3ba8b3d74e27e8e9d9d5550f31ba
fetch dvtm-0.15.tar.gz https://github.com/martanne/dvtm/archive/refs/tags/v0.15.tar.gz \
    496eada13d8abaa8d772279746f78b0c6fed11b560599490f3e70ebc21197bf0
fetch ncdu-1.22.tar.gz https://dev.yorhel.nl/download/ncdu-1.22.tar.gz \
    0ad6c096dc04d5120581104760c01b8f4e97d4191d6c9ef79654fa3c691a176b
fetch jq-1.8.2.tar.gz https://github.com/jqlang/jq/releases/download/jq-1.8.2/jq-1.8.2.tar.gz \
    71b8d6e8f5fe81f6c6d0d110e3892251f6ce76ed095abd315e26e6e1193af3af
fetch micropython-1.29.0.tar.gz https://github.com/micropython/micropython/archive/refs/tags/v1.29.0.tar.gz \
    8072f6ad42c0e3bbfca88ed09bf8f1106ce207a39129af920f70081b8f2fe286
fetch htop-3.5.3.tar.xz https://github.com/htop-dev/htop/releases/download/3.5.3/htop-3.5.3.tar.xz \
    a8b164386494cb85bb255a415a3f5f80afe7a0c4491da5d113b3a0f951087e65
fetch frotz-2.55.tar.gz https://gitlab.com/DavidGriffith/frotz/-/archive/2.55/frotz-2.55.tar.gz \
    a8c4c4d79a2aa9b39e0efbbd3e0803f1dc5ef36f75f2c2945e295d190575eb09
fetch curl-8.22.0.tar.xz https://github.com/curl/curl/releases/download/curl-8_22_0/curl-8.22.0.tar.xz \
    f7ef3ae8a22e521f289803fe93543eb64c329b58aa73a9e224dfd915a2a5f4f7
fetch mbedtls-3.6.7.tar.bz2 https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 \
    a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6
fetch libressl-4.3.3.tar.gz https://cdn.openbsd.org/pub/OpenBSD/LibreSSL/libressl-4.3.3.tar.gz \
    ff97c432457f349e6ba3d416ab903bc7468f1436f0f32efe5fff808de292c7b8
fetch dejavu-fonts-ttf-2.37.tar.bz2 \
    https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.tar.bz2 \
    fa9ca4d13871dd122f61258a80d01751d603b4d3ee14095d65453b4e846e17d7
fetch liberation-fonts-ttf-2.1.5.tar.gz \
    https://github.com/liberationfonts/liberation-fonts/files/7261482/liberation-fonts-ttf-2.1.5.tar.gz \
    7191c669bf38899f73a2094ed00f7b800553364f90e2637010a69c0e268f25d0
fetch cacert-2026-09-25.pem https://curl.se/ca/cacert-2026-09-25.pem \
    a41b5d356aea97a529fe27e0f7316d2f9d946d75927476cf9cf1b90637d00505
zork=https://raw.githubusercontent.com/historicalsource
fetch zork1.z3 "$zork/zork1/97b7b3d68c075dd9af7da499c3e9690ada3471fd/COMPILED/zork1.z3" \
    37084966477dff679282de42974b2077156b1bd68fad92a65d4ea94d8eb64d79
fetch zork2.z3 "$zork/zork2/3da9661098809788a99cef00f00c865c6c204f96/COMPILED/zork2.z3" \
    3ae7d5558943e9721f3e4b273c8a7faec1a03a604e1ae4ee1cde472c21cb24ac
fetch zork3.z3 "$zork/zork3/3ec9ed412b5f3cafe65d83c727d07db1fe4a86a8/COMPILED/zork3.z3" \
    b637a242865d059890184164ce8dec28554cc80901dcbf26c740b2d1ed0d4eb8
fetch zork-LICENSE "$zork/zork1/97b7b3d68c075dd9af7da499c3e9690ada3471fd/LICENSE" \
    98a96c38494df4963951e32b81ec4effebe4c8a812ab8d9e7d185d59e86fe2fc
crypto=https://raw.githubusercontent.com/B-Con/crypto-algorithms/cfbde48414baacf51fc7c74f275190881f037d32
fetch sha256.c "$crypto/sha256.c" 4cbc93d30c28cee2a056d6f52cbce6e66ff9450f1d712ee4f59761b9b4486fad
fetch sha256.h "$crypto/sha256.h" a946e621775fd8251624e77d19f25407c4b6074ab780027e5797b77c7acf79b5

unpack libvterm-0.3.3.tar.gz libvterm-0.3.3 terminal/patches/libvterm-rep.patch
unpack compiler-rt-21.1.8.src.tar.xz compiler-rt-21.1.8.src
mkdir -p "$deps/crypto-algorithms"
cp "$deps/archives/sha256.c" "$deps/archives/sha256.h" "$deps/crypto-algorithms/"
printf '%s\n' "Dependencies ready in $deps"
