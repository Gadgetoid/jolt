workspace=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tools=${JORNADA_TOOLS:-"$workspace/../jornada-72x-tools"}
llvm=${JORNADA_LLVM_ROOT:-"$tools/llvm"}
deps=${JOLT_DEPS:-"$workspace/build/deps"}
sysroot="$workspace/build/guest/sysroot"
output="$workspace/build/apps"
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
jolt_llvm=$llvm
jolt_sysroot=$sysroot
jolt_linker_script="$workspace/guest/guest.ld"
source "$workspace/tools/sdk/flags.sh"
APP_CC=$JOLT_CC
APP_CFLAGS=$JOLT_CFLAGS
APP_LDFLAGS=$JOLT_LDFLAGS
APP_LIBS=$JOLT_LIBS
mkdir -p "$output/bin" "$output/src" "$output/toolchain"
export GIT_CEILING_DIRECTORIES="$output/src"
APP_WRAPPER="$output/toolchain/jolt-cc"
jolt_write_wrapper "$APP_WRAPPER"
APP_CONFIGURE_ENVIRONMENT=(CC="$APP_WRAPPER" AR="$llvm/bin/llvm-ar" RANLIB="$llvm/bin/llvm-ranlib" STRIP="$llvm/bin/llvm-strip")

unpack_source() {
    rm -rf "$output/src/$2"
    tar -xf "$deps/archives/$1" -C "$output/src"
}

finish_app() {
    local binary=$1 name=$2
    jolt_check_binary "$binary"
    "$llvm/bin/llvm-strip" -o "$output/bin/$name" "$binary"
}
