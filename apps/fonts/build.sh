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
