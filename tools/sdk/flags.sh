jolt_target_flags="--target=arm-linux-gnueabi -march=armv4 -marm -mfloat-abi=soft -fno-unwind-tables -fno-asynchronous-unwind-tables"
JOLT_CC="$jolt_llvm/bin/clang $jolt_target_flags -nostdinc -isystem $jolt_sysroot/include"
JOLT_CFLAGS="-Os -ffunction-sections -fdata-sections -fno-stack-protector"
JOLT_LDFLAGS="-fuse-ld=lld -nostdlib -static -Wl,-T,$jolt_linker_script -Wl,--gc-sections -Wl,--build-id=none -Wl,-z,max-page-size=4096 -Wl,-u,_start -Wl,-u,__aeabi_read_tp -L$jolt_sysroot"
JOLT_LIBS="-Wl,--start-group -ljoltcrt -lc -lm -ljoltrt -Wl,--end-group"

jolt_write_wrapper() {
    cat > "$1" <<WRAPPER
#!/bin/sh
for argument in "\$@"; do
    case "\$argument" in
        -c|-S|-E|-M|-MM) exec $JOLT_CC $JOLT_CFLAGS "\$@" ;;
    esac
done
exec $JOLT_CC $JOLT_CFLAGS "\$@" $JOLT_LDFLAGS $JOLT_LIBS
WRAPPER
    chmod +x "$1"
}

jolt_check_binary() {
    local binary=$1 disassembly
    disassembly=$("$jolt_llvm/bin/llvm-objdump" -d "$binary")
    if grep -E -i '\s(svc|swi|bx|blx|clz|ldrex|strex|vldr|vstr|vmov|vmrs|vmsr|mcr|mrc)\s' <<< "$disassembly"; then
        printf 'Refused: unsupported instruction in %s\n' "$binary" >&2
        return 1
    fi
    if ! "$jolt_llvm/bin/llvm-readelf" -n "$binary" | grep -q JoLT; then
        printf 'Refused: %s has no JoLT ABI note\n' "$binary" >&2
        return 1
    fi
}
