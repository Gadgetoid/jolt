#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../../common.sh"

unpack_source ncurses-6.6.tar.gz ncurses-6.6
source_dir="$output/src/ncurses-6.6"
host_dir="$output/src/ncurses-host"
rm -rf "$host_dir"
mkdir -p "$host_dir"
(
    cd "$host_dir"
    CC=cc "$source_dir/configure" --without-shared --without-debug --without-cxx --without-cxx-binding \
        --without-ada --without-tests --without-manpages --without-pkg-config --disable-widec > configure.log
    make -j"$jobs" -C include > make.log
    make -j"$jobs" -C ncurses >> make.log
    make -j"$jobs" -C progs tic infocmp >> make.log
)
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os ./configure --host=arm-linux-gnueabi --build="$(./config.guess)" \
        --prefix="$sysroot" --includedir="$sysroot/include" --libdir="$sysroot" \
        --with-build-cc=cc --without-shared --with-normal --without-debug --without-profile \
        --without-cxx --without-cxx-binding --without-ada --without-progs --without-tests --without-manpages \
        --without-pkg-config --disable-widec --enable-overwrite --disable-database --disable-home-terminfo \
        --with-fallbacks=xterm-256color,xterm,vt102,vt100,linux,ansi --disable-stripping \
        --with-tic-path="$host_dir/progs/tic" --with-infocmp-path="$host_dir/progs/infocmp" \
        > configure.log
    make -j"$jobs" libs > make.log
    make install.libs install.includes > install.log
)
