#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source dropbear-2026.94.tar.bz2 dropbear-2026.94
source_dir="$output/src/dropbear-2026.94"
cp "$(dirname "$0")/localoptions.h" "$source_dir/localoptions.h"
(
    cd "$source_dir"
    env "${APP_CONFIGURE_ENVIRONMENT[@]}" ./configure --host=arm-linux-gnueabi --enable-static --disable-harden \
        --disable-zlib --disable-syslog --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp \
        --disable-wtmpx --disable-pututline --disable-pututxline --disable-openpty > configure.log
    make -j"$jobs" PROGRAMS="dbclient dropbearkey" > make.log
)
finish_app "$source_dir/dbclient" dbclient
finish_app "$source_dir/dropbearkey" dropbearkey
