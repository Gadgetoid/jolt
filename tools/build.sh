#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
grimace=${GRIMACE:?set GRIMACE to a grimace checkout}
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1791849600}
export GRIMACE_LLVM_ROOT=${GRIMACE_LLVM_ROOT:-"$grimace/llvm"}
cd "$workspace"
bash tools/fetch.sh
bash guest/build.sh
bash apps/build.sh
cmake -S . -B build/ce -G Ninja -DCMAKE_TOOLCHAIN_FILE="$grimace/cmake/toolchain.cmake" -DGRIMACE_ARCH=arm -DGRIMACE_CE_VERSION=3 \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DGRIMACE_LLVM_ROOT="$GRIMACE_LLVM_ROOT"
cmake --build build/ce
cc -std=c11 -Wall -Wextra -Werror worker/path.c tests/path-test.c -Iworker -o build/path-test
build/path-test
cc -std=c11 -Wall -Wextra -Werror terminal/boxdraw.c tests/boxdraw-test.c -Iterminal -o build/boxdraw-test
build/boxdraw-test
bash tools/package.sh
