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
mkdir -p build/dist/sources
cp build/deps/archives/* build/dist/sources/
cp guest/busybox/busybox.config build/dist/sources/
tar -czf "build/dist/jolt-$version-third-party-sources.tar.gz" -C build/dist sources
rm -rf build/dist/sources
shasum -a 256 build/dist/*.zip build/dist/*.tar.gz
