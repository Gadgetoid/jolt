#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source lua-5.4.7.tar.gz lua-5.4.7
source_dir="$output/src/lua-5.4.7/src"
objects=()
for file in "$source_dir"/*.c; do
    name=$(basename "$file" .c)
    if [[ $name == luac ]]; then continue; fi
    $APP_CC $APP_CFLAGS -DLUA_USE_POSIX -c "$file" -o "$source_dir/$name.o"
    if [[ $name != lua ]]; then objects+=("$source_dir/$name.o"); fi
done
$APP_CC $APP_LDFLAGS -o "$source_dir/lua.elf" "$source_dir/lua.o" "${objects[@]}" $APP_LIBS
finish_app "$source_dir/lua.elf" lua
