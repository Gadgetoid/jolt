workspace=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
tools=${JORNADA_TOOLS:-"$workspace/../jornada-72x-tools"}
llvm=${JORNADA_LLVM_ROOT:-"$tools/llvm"}
deps=${JOLT_DEPS:-"$workspace/build/deps"}
sysroot="$workspace/build/guest/sysroot"
output="$workspace/build/apps"
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
target_flags="--target=arm-linux-gnueabi -march=armv4 -marm -mfloat-abi=soft -fno-unwind-tables -fno-asynchronous-unwind-tables"
APP_CC="$llvm/bin/clang $target_flags -nostdinc -isystem $sysroot/include"
APP_CFLAGS="-Os -ffunction-sections -fdata-sections -fno-stack-protector"
APP_LDFLAGS="-fuse-ld=lld -nostdlib -static -Wl,-T,$workspace/guest/guest.ld -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,max-page-size=4096 -Wl,-u,_start -Wl,-u,__aeabi_read_tp -L$sysroot"
APP_LIBS="-Wl,--start-group -ljoltcrt -lc -lm -ljoltrt -Wl,--end-group"
mkdir -p "$output/bin" "$output/src" "$output/toolchain"
APP_WRAPPER="$output/toolchain/jolt-cc"
cat > "$APP_WRAPPER" <<WRAPPER
#!/bin/sh
for argument in "\$@"; do
    case "\$argument" in
        -c|-S|-E|-M|-MM) exec $APP_CC $APP_CFLAGS "\$@" ;;
    esac
done
exec $APP_CC $APP_CFLAGS "\$@" $APP_LDFLAGS $APP_LIBS
WRAPPER
chmod +x "$APP_WRAPPER"
APP_CONFIGURE_ENVIRONMENT=(CC="$APP_WRAPPER" AR="$llvm/bin/llvm-ar" RANLIB="$llvm/bin/llvm-ranlib" STRIP="$llvm/bin/llvm-strip")

unpack_source() {
    rm -rf "$output/src/$2"
    tar -xf "$deps/archives/$1" -C "$output/src"
}

finish_app() {
    local binary=$1 name=$2
    "$llvm/bin/llvm-objdump" -d "$binary" > "$output/$name-disassembly.txt"
    if grep -E -i '\s(svc|swi|bx|blx|clz|ldrex|strex|vldr|vstr|vmov|vmrs|vmsr|mcr|mrc)\s' "$output/$name-disassembly.txt"; then
        printf 'Refused: unsupported instruction in %s\n' "$name" >&2
        return 1
    fi
    if ! "$llvm/bin/llvm-readelf" -n "$binary" | grep -q JoLT; then
        printf 'Refused: %s has no JoLT ABI note\n' "$name" >&2
        return 1
    fi
    "$llvm/bin/llvm-strip" -o "$output/bin/$name" "$binary"
}
