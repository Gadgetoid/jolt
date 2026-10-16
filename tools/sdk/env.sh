jolt_sdk=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
jolt_llvm=${JORNADA_LLVM_ROOT:?set JORNADA_LLVM_ROOT to the Windows CE LLVM}
jolt_sysroot="$jolt_sdk/sysroot"
jolt_linker_script="$jolt_sdk/lib/guest.ld"
source "$jolt_sdk/lib/flags.sh"
mkdir -p "$jolt_sdk/bin"
jolt_write_wrapper "$jolt_sdk/bin/jolt-cc"
export JOLT_SDK=$jolt_sdk
export JOLT_ABI=$(< "$jolt_sdk/abi")
export CC="$jolt_sdk/bin/jolt-cc"
export AR="$jolt_llvm/bin/llvm-ar" RANLIB="$jolt_llvm/bin/llvm-ranlib" STRIP="$jolt_llvm/bin/llvm-strip"
