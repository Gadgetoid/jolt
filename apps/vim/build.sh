#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

features=${VIM_FEATURES:-tiny}
unpack_source vim-9.2.1178.tar.gz vim-9.2.1178
source_dir="$output/src/vim-9.2.1178/src"
patch -d "$output/src/vim-9.2.1178" -p1 < "$(dirname "$0")/zero-wait.patch" > /dev/null
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" CFLAGS=-Os PKG_CONFIG=false \
        vim_cv_toupper_broken=no vim_cv_terminfo=yes vim_cv_tgetent=zero vim_cv_getcwd_broken=no \
        vim_cv_stat_ignores_slash=no vim_cv_memmove_handles_overlap=yes vim_cv_timer_create=no \
        vim_cv_timer_create_works=no ac_cv_small_wchar_t=no ac_cv_sizeof_int=4 \
        vim_cv_uname_output=Linux vim_cv_uname_r_output=5.10.0 vim_cv_uname_m_output=armv4l \
        ac_cv_header_dlfcn_h=no ac_cv_func_dlopen=no ac_cv_lib_dl_dlopen=no \
        ./configure --host=arm-linux-gnueabi --prefix=/usr --with-features="$features" --with-tlib=ncurses \
        --disable-gui --without-x --disable-netbeans --disable-channel --disable-terminal --disable-xsmp \
        --disable-gpm --disable-sysmouse --disable-nls --disable-acl --disable-libsodium --disable-canberra \
        --disable-selinux --disable-smack --disable-xattr --disable-icon-cache-update \
        --disable-desktop-database-update --disable-darwin --with-compiledby=JoLT \
        > configure.log
    make -j"$jobs" vim > make.log
)
finish_app "$source_dir/vim" vim
mkdir -p "$output/share/vim/vim92"
cp "$(dirname "$0")/defaults.vim" "$output/share/vim/vim92/defaults.vim"
