#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/../common.sh"

fonts="$output/share/fonts"
mkdir -p "$fonts"
tar -xjf "$deps/archives/dejavu-fonts-ttf-2.37.tar.bz2" -C "$fonts" --strip-components=2 \
    dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono.ttf dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono-Bold.ttf
tar -xjf "$deps/archives/dejavu-fonts-ttf-2.37.tar.bz2" -O dejavu-fonts-ttf-2.37/LICENSE > "$fonts/LICENSE-DejaVu"
tar -xzf "$deps/archives/liberation-fonts-ttf-2.1.5.tar.gz" -C "$fonts" --strip-components=1 \
    liberation-fonts-ttf-2.1.5/LiberationMono-Regular.ttf liberation-fonts-ttf-2.1.5/LiberationMono-Bold.ttf
tar -xzf "$deps/archives/liberation-fonts-ttf-2.1.5.tar.gz" -O liberation-fonts-ttf-2.1.5/LICENSE > "$fonts/LICENSE-Liberation"

bitmaps="$output/src/bitmap-fonts"
rm -rf "$bitmaps"
mkdir -p "$bitmaps"
tar -xzf "$deps/archives/spleen-2.2.0.tar.gz" -C "$bitmaps"
tar -xzf "$deps/archives/terminus-font-4.49.1.tar.gz" -C "$bitmaps"
for size in 5x8 6x12 8x16 12x24; do
    height=${size#*x}
    node "$workspace/tools/bdf2jbf.js" "$bitmaps/spleen-2.2.0/spleen-$size.bdf" "$fonts/spleen-$height.jbf" \
        - "$fonts/spleen-$height-bold.jbf"
done
cp "$bitmaps/spleen-2.2.0/LICENSE" "$fonts/LICENSE-Spleen"
for height in 12 14 16 18 20 24; do
    node "$workspace/tools/bdf2jbf.js" "$bitmaps/terminus-font-4.49.1/ter-u${height}n.bdf" "$fonts/terminus-$height.jbf" \
        "$bitmaps/terminus-font-4.49.1/ter-u${height}b.bdf" "$fonts/terminus-$height-bold.jbf"
done
cp "$bitmaps/terminus-font-4.49.1/OFL.TXT" "$fonts/LICENSE-Terminus"
