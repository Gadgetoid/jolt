#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
tools=${JORNADA_TOOLS:-"$workspace/../jornada-72x-tools"}
export JORNADA_TOOLS="$tools"
export SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1791849600}
export JORNADA_LLVM_ROOT=${JORNADA_LLVM_ROOT:-"$tools/llvm"}
cd "$workspace"
bash tools/fetch.sh
bash guest/build.sh
bash apps/build.sh
cmake -S . -B build/ce -G Ninja -DCMAKE_TOOLCHAIN_FILE="$tools/cmake/jornada-ce.cmake" \
    -DCMAKE_BUILD_TYPE=MinSizeRel -DJORNADA_LLVM_ROOT="$JORNADA_LLVM_ROOT"
cmake --build build/ce
cc -std=c11 -Wall -Wextra -Werror worker/path.c tests/path-test.c -Iworker -o build/path-test
build/path-test
cc -std=c11 -Wall -Wextra -Werror terminal/boxdraw.c tests/boxdraw-test.c -Iterminal -o build/boxdraw-test
build/boxdraw-test
bash tools/package.sh
