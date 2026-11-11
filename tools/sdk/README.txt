JoLT SDK

Builds Linux programs for JoLT: static ARMv4 ELF files with the JoLT ABI note.

Needs the Windows CE LLVM from https://github.com/grimace-sdk/llvm-builds

    export GRIMACE_LLVM_ROOT=/path/to/velo-llvm-v3-linux-x86_64
    source jolt-sdk/env.sh    # from bash
    $CC -o hello hello.c
    jolt_check_binary hello

env.sh sets CC, AR, RANLIB and STRIP for configure scripts and Makefiles, and
JOLT_ABI to the ABI version these libraries and the linker script are for.
jolt_check_binary refuses instructions the StrongARM lacks and binaries
without the JoLT ABI note.

lib/bdf2jbf.js (Node.js) converts BDF fonts to JoLT's bitmap font format:
node lib/bdf2jbf.js font.bdf name-16.jbf [bold.bdf|-] name-16-bold.jbf

sysroot holds musl, ncurses (narrow, with built-in terminfo), LibreSSL and
Mbed TLS. Licences are listed in THIRD-PARTY.md.
