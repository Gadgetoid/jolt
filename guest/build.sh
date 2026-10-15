#!/usr/bin/env bash
set -euo pipefail

workspace=$(cd "$(dirname "$0")/.." && pwd)
tools=${JORNADA_TOOLS:-"$workspace/../jornada-72x-tools"}
llvm=${JORNADA_LLVM_ROOT:-"$tools/llvm"}
deps=${JOLT_DEPS:-"$workspace/build/deps"}
guest="$workspace/guest"
build="$workspace/build/guest"
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
make_tool=$(command -v gmake || command -v make)

rm -rf "$build/src"
mkdir -p "$build/src" "$build/musl" "$build/sysroot"
tar -xf "$deps/archives/musl-1.2.5.tar.gz" -C "$build/src"
tar -xf "$deps/archives/busybox-1.38.0.tar.bz2" -C "$build/src"
musl="$build/src/musl-1.2.5"
busybox="$build/src/busybox-1.38.0"

patch -d "$musl" -p1 < "$guest/musl/musl-armv4.patch"
cp "$guest/musl/syscall_arch.h" "$guest/musl/pthread_arch.h" "$guest/musl/atomic_arch.h" "$musl/arch/arm/"
cp "$guest/musl/set-thread-area.c" "$musl/src/thread/arm/__set_thread_area.c"
cp "$guest/musl/clone.c" "$musl/src/thread/arm/clone.c"
cp "$guest/musl/read-tp.S" "$musl/src/thread/arm/__aeabi_read_tp.s"
cp "$guest/musl/restore.s" "$musl/src/signal/arm/restore.s"
rm -f "$musl/src/process/arm/vfork.s" "$musl/src/thread/arm/clone.s"

flags="--target=arm-linux-gnueabi -march=armv4 -marm -mfloat-abi=soft -fno-unwind-tables -fno-asynchronous-unwind-tables"
(
    cd "$build/musl"
    CC="$llvm/bin/clang $flags" AR="$llvm/bin/llvm-ar" RANLIB="$llvm/bin/llvm-ranlib" \
        "$musl/configure" --target=arm-linux-gnueabi --disable-shared --prefix="$build/sysroot" \
        CFLAGS="-Os -ffunction-sections -fdata-sections" LIBCC=""
    "$make_tool" -j"$jobs" ARCH_SRCS="$musl/src/thread/arm/__set_thread_area.c $musl/src/thread/arm/clone.c $musl/src/signal/arm/restore.s $musl/src/signal/arm/sigsetjmp.s" \
        lib/libc.a lib/libm.a install-headers
)

cp -R "$guest/include/." "$build/sysroot/include/"

cmake -S "$guest/runtime" -B "$build/runtime" -G Ninja \
    -DCMAKE_SYSTEM_NAME=Generic -DCMAKE_C_COMPILER="$llvm/bin/clang" \
    -DCMAKE_ASM_COMPILER="$llvm/bin/clang" -DCMAKE_C_COMPILER_TARGET=arm-linux-gnueabi \
    -DCMAKE_ASM_COMPILER_TARGET=arm-linux-gnueabi -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_AR="$llvm/bin/llvm-ar" -DCMAKE_RANLIB="$llvm/bin/llvm-ranlib" \
    -DBUILTINS_SOURCE="$deps/compiler-rt-21.1.8.src/lib/builtins" -DSYSROOT_INCLUDE="$build/sysroot/include"
cmake --build "$build/runtime"

objects=()
for source in entry.S start.c setjmp.S; do
    object="$build/${source%.*}.o"
    "$llvm/bin/clang" $flags -nostdinc -isystem "$build/sysroot/include" -I "$workspace/common" -Os -c "$guest/crt/$source" -o "$object"
    objects+=("$object")
done
"$llvm/bin/clang" $flags -c "$guest/musl/read-tp.S" -o "$build/read-tp.o"
rm -f "$build/sysroot/libjoltcrt.a"
"$llvm/bin/llvm-ar" rcs "$build/sysroot/libjoltcrt.a" "${objects[@]}" "$build/read-tp.o"
cp "$build/musl/lib/libc.a" "$build/musl/lib/libm.a" "$build/sysroot/"
cp "$build/runtime/libjolt_builtins.a" "$build/sysroot/libjoltrt.a"

(
    cd "$busybox"
    "$make_tool" allnoconfig HOSTCC=cc
    node "$guest/busybox/configure.js" .config "$guest/busybox/busybox.config"
    (yes "" || true) | "$make_tool" oldconfig HOSTCC=cc > /dev/null
    "$make_tool" -j"$jobs" HOSTCC=cc AR="$llvm/bin/llvm-ar" NM="$llvm/bin/llvm-nm" \
        CC="$llvm/bin/clang $flags -nostdinc -isystem $build/sysroot/include" \
        CONFIG_EXTRA_CFLAGS="-ffunction-sections -fdata-sections -fno-stack-protector" \
        CONFIG_EXTRA_LDFLAGS="-fuse-ld=lld -nostdlib -static -Wl,-T,$guest/guest.ld -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,max-page-size=4096 -Wl,-u,_start -Wl,-u,__aeabi_read_tp -L$build/sysroot" \
        SKIP_STRIP=y
)
cp "$busybox/busybox_unstripped" "$build/busybox.elf"
"$llvm/bin/llvm-readelf" -h -l -A "$build/busybox.elf" > "$build/busybox-elf.txt"
"$llvm/bin/llvm-objdump" -d "$build/busybox.elf" > "$build/busybox-disassembly.txt"
if grep -E -i '\s(svc|swi|bx|blx|clz|ldrex|strex|vldr|vstr|vmov|vmrs|vmsr|mcr|mrc)\s' "$build/busybox-disassembly.txt"; then
    printf '%s\n' 'Refused: unsupported instruction in BusyBox image' >&2
    exit 1
fi
if ! "$llvm/bin/llvm-readelf" -n "$build/busybox.elf" | grep -q JoLT; then
    printf '%s\n' 'Refused: BusyBox image has no JoLT ABI note' >&2
    exit 1
fi
node "$workspace/tools/applets.js" "$busybox/include/applet_tables.h" "$build/applets.txt"
node "$workspace/tools/manifest.js" "$build/busybox.elf" "$build/guest-manifest.h" "$build/applets.txt"
