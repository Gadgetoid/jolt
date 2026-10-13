# JoLT - Jornada Linux Terminal

A terminal for the HP Jornada 720 that runs BusyBox `ash` and its applets as native ARM Linux programs on Windows CE 3.0, alongside CE itself.

JoLT is not a Linux kernel or a VM. A small compatibility layer loads a statically linked, hash-checked BusyBox ELF into ordinary CE processes and implements the Linux system calls it uses: processes with real `fork`, `execve` and `wait`, pipes, signals and job control, a tty with a line discipline, and a root filesystem on the CF card.

## Install

Download a release `.zip`, copy its `JoLT` folder to the root of a CompactFlash card and run `\Storage Card\JoLT\JoLT-Term.exe`.

- `help` lists shell built-ins; `jolt help` lists display settings (`jolt fs`, `jolt dm`, `jolt font 12`, `jolt theme solarized`).
- `jolt mount '\My Documents' /mnt/docs` maps a CE folder into the tree (`/etc/fstab` is mounted at login); `jolt open FILE` opens a file in its CE application; `jolt battery` shows power status; `/dev/clipboard` reads and writes the CE clipboard.
- `JoLT-Term.exe -e "ssh user@host"` runs a command and closes when it exits (`--hold` drops to a shell instead); `jolt shortcut "Laptop" "ssh user@host"` puts one on the desktop (`-s` for the Start menu).
- Programs built with the JoLT toolchain (apps/) carry an ABI note the loader checks; set `JOLT_ALLOW_UNMARKED=1` to try a static ARMv4 binary built elsewhere.
- `/dev/ttyS0`-`/dev/ttyS3` are CE's `COM1:`-`COM4:` (COM1 is the serial cable, busy while PPP/ActiveSync uses it; COM3 is IrCOMM, COM4 raw IR). `microcom -s 115200 /dev/ttyS0` talks to whatever is on the cable.
- `JoLT\root` is `/`. Ctrl-C, Ctrl-Z, `fg`, `bg` and `jobs` work. Shift+PgUp/PgDn scrolls back.
- The Jornada has no function keys: Ctrl+1 to Ctrl+0 send F1 to F10, Ctrl+- and Ctrl+= send F11 and F12 (htop: Ctrl+0 quits). `jolt keys` shows the raw keyboard messages JoLT receives.
- `exit` returns to the desktop.

## Building

Needs macOS or Linux with CMake, Ninja, GNU make, Node.js, curl and zip, plus:

- [jornada-72x-tools](https://github.com/Gadgetoid/jornada-72x-tools) beside this repository, or `JORNADA_TOOLS` pointing at it.
- The Windows CE ARM LLVM from [velo-llvm-builds](https://github.com/Gadgetoid/velo-llvm-builds/releases) (v3), linked as `jornada-72x-tools/llvm` or set as `JORNADA_LLVM_ROOT`.

```sh
tools/build.sh      # fetch pinned sources, build BusyBox, JoLT-Term and JoltWorker, package
tools/card.sh [FILE] # optional: emulator card image, build/jolt-card.cf by default
tests/emulator.sh   # QA in jornada-72x-emu
```

`tools/build.sh` writes `build/dist/jolt-<version>.zip` and `jolt-<version>-third-party-sources.tar.gz`. GitHub Actions (`.github/workflows/build.yml`) runs the same build on Ubuntu with the pinned toolkit commit and LLVM release, uploads both as artifacts, and attaches them to a release for tags. Emulator QA stays local.

## Layout

| Path | Contents |
| --- | --- |
| `terminal/` | `JoLT-Term.exe`: window, libvterm display, scrollback, themes, tty line discipline, process broker |
| `worker/` | `JoltWorker.exe`: one per Linux process; ELF loader, guest memory, fork, signals, system calls |
| `common/` | State shared between them (`kernel.h`), Linux ABI definitions (`linux.h`), SHA-256 |
| `guest/` | musl and BusyBox build for the controlled ARMv4 profile |
| `rootfs/` | Files installed under `/` |
| `tests/` | Emulator QA scripts and checks, path unit test |
| `tools/` | Fetch, build, package and card scripts |
| `docs/` | Architecture notes and the original design specification |

See [docs/architecture.md](docs/architecture.md) for how it works.

## Licence

JoLT is MIT licensed. Release archives include BusyBox (GPL-2.0) and other components; see [THIRD-PARTY.md](THIRD-PARTY.md).
