#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

source_dir="$(dirname "$0")"
build_dir="$output/src/jpm"
mkdir -p "$build_dir"
"$APP_WRAPPER" -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -I"$deps/crypto-algorithms" \
    -o "$build_dir/jpm" "$source_dir/jpm.c" "$deps/crypto-algorithms/sha256.c"
finish_app "$build_dir/jpm" jpm
