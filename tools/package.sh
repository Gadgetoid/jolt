#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
cd "$workspace"
version=${JOLT_VERSION:-$(git describe --tags --always --dirty 2>/dev/null || echo dev)}
stage=build/dist/stage/JoLT
rm -rf build/dist
mkdir -p "$stage/root"
cp build/ce/JoLT-Term.exe build/ce/JoltWorker.exe build/guest/busybox.elf assets/splash.bmp "$stage/"
cp -R rootfs/. "$stage/root/"
mkdir -p "$stage/root/usr/bin"
for app in build/apps/bin/*; do
    [[ -f $app ]] || continue
    cp "$app" "$stage/root/usr/bin/"
done
if [[ -d build/apps/share ]]; then
    mkdir -p "$stage/root/usr/share"
    cp -R build/apps/share/. "$stage/root/usr/share/"
fi
find "$stage" -name .keep -delete
mkdir -p "$stage/root/tmp"
for pair in "LICENSE:LICENSE.txt" "THIRD-PARTY.md:THIRD-PARTY.txt" "docs/card-readme.txt:README.txt"; do
    sed 's/$/\r/' "${pair%%:*}" > "$stage/${pair##*:}"
done
(cd build/dist/stage && zip -qr -X "../jolt-$version.zip" JoLT)
gpl_sources=(busybox-1.38.0.tar.bz2 links-2.30.tar.bz2 htop-3.5.3.tar.xz frotz-2.55.tar.gz nano-9.2.tar.xz
    musl-1.2.5.tar.gz compiler-rt-21.1.8.src.tar.xz ncurses-6.6.tar.gz)
mkdir -p build/dist/sources
for archive in "${gpl_sources[@]}"; do cp "build/deps/archives/$archive" build/dist/sources/; done
cp guest/busybox/busybox.config apps/frotz/no-root-check.patch build/dist/sources/
cp -R guest/musl build/dist/sources/musl-patches
printf '%s\n' "Build scripts and patches: https://github.com/Gadgetoid/jolt (guest/, apps/, tools/)" > build/dist/sources/README.txt
tar -cf "build/dist/jolt-$version-gpl-sources.tar" -C build/dist sources
rm -rf build/dist/sources
shasum -a 256 build/dist/*.zip build/dist/*.tar
