#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

unpack_source sfeed-2.4.tar.gz sfeed-2.4
source_dir="$output/src/sfeed-2.4"
programs=(sfeed sfeed_curses sfeed_plain sfeed_opml_import sfeed_xmlenc)
scripts=(sfeed_update sfeed_markread sfeed_content sfeed_opml_export)
make -C "$source_dir" "${programs[@]}" CC="$APP_WRAPPER" AR="$llvm/bin/llvm-ar" RANLIB="$llvm/bin/llvm-ranlib" \
    CFLAGS="-std=c99 -Os" SFEED_THEME=newsboat SFEED_CURSES_LDFLAGS="-lncurses" > "$source_dir/make.log" 2>&1
for program in "${programs[@]}"; do finish_app "$source_dir/$program" "$program"; done
for script in "${scripts[@]}"; do install -m 755 "$source_dir/$script" "$output/bin/$script"; done
install -m 755 "$(dirname "$0")/sfeed_htmlconv" "$output/bin/sfeed_htmlconv"
install -m 755 "$(dirname "$0")/feeds" "$output/bin/feeds"
mkdir -p "$output/share/sfeed"
cp "$(dirname "$0")/sfeedrc" "$output/share/sfeed/sfeedrc.example"
cp "$source_dir/LICENSE" "$output/share/sfeed/LICENSE"
