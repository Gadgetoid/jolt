#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
version=$1
name="jolt-sdk-$version"
stage="$workspace/build/dist/sdk/$name"
rm -rf "$workspace/build/dist/sdk"
mkdir -p "$stage/lib" "$stage/sysroot"
cp -R "$workspace/build/guest/sysroot/include" "$stage/sysroot/"
cp "$workspace"/build/guest/sysroot/*.a "$stage/sysroot/"
cp "$workspace/guest/guest.ld" "$workspace/tools/sdk/flags.sh" "$workspace/tools/bdf2jbf.js" "$stage/lib/"
cp "$workspace/tools/sdk/env.sh" "$workspace/tools/sdk/README.txt" "$workspace/THIRD-PARTY.md" "$stage/"
sed -n 's/^#define JOLT_ABI_VERSION //p' "$workspace/common/abi.h" > "$stage/abi"
printf '%s\n' "$version" > "$stage/version"
tar -czf "$workspace/build/dist/$name.tar.gz" -C "$workspace/build/dist/sdk" "$name"
rm -rf "$workspace/build/dist/sdk"
