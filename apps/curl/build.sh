#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source curl-8.22.0.tar.xz curl-8.22.0
source_dir="$output/src/curl-8.22.0"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os PKG_CONFIG=false \
        LIBS="-lmbedtls -lmbedx509 -lmbedcrypto" \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --disable-shared --enable-static \
        --with-mbedtls --without-libpsl --without-zlib --without-brotli --without-zstd \
        --without-nghttp2 --without-libidn2 --without-libssh2 --disable-threaded-resolver --disable-ipv6 \
        --disable-ldap --disable-ldaps --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
        --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt --disable-smb \
        --disable-manual --disable-docs --disable-unix-sockets --disable-ntlm --disable-kerberos-auth \
        --with-ca-bundle=/etc/ssl/cert.pem --without-ca-path --enable-ssls-export > configure.log
    make -j"$jobs" > make.log 2>&1
)
finish_app "$source_dir/src/curl" curl
