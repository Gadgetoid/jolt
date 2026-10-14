#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source bash-5.3.tar.gz bash-5.3
source_dir="$output/src/bash-5.3"
(
    cd "$source_dir"
    sed -i.orig 's|^/\* #define SYS_BASHRC "/etc/bash.bashrc" \*/|#define SYS_BASHRC "/etc/bash.bashrc"|' config-top.h
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os PKG_CONFIG=false CC_FOR_BUILD=cc \
        bash_cv_getcwd_malloc=yes bash_cv_job_control_missing=present bash_cv_sys_named_pipes=present \
        bash_cv_func_sigsetjmp=present bash_cv_printf_a_format=yes bash_cv_unusable_rtsigs=no \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --sysconfdir=/etc \
        --disable-nls --without-bash-malloc --enable-static-link \
        --with-curses > configure.log
    make -j"$jobs" > make.log 2>&1
)
finish_app "$source_dir/bash" bash
