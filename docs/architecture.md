# Architecture

## Processes

`JoLT-Term.exe` owns the window and acts as a small kernel. It creates a named shared mapping (`common/kernel.h`) holding the process table, open file descriptions, pipes, tty rings and termios, guarded by a named mutex, with one auto-reset wake event per process slot. It verifies `busybox.elf` against the SHA-256 compiled into it and copies it into a shared image mapping.

Each Linux process is a `JoltWorker.exe` CE process. Guest code runs natively in System mode in a range of the worker's address space, `0x00c10000-0x01400000`, which the worker reserves at startup. Programs are linked at `0x00c10000` (JoLT ABI note version 2). The same layout works on CE 3.0 and CE 6.0; CE 6.0 refuses commits in the low range the earlier layout used. On CE 3.0, DLLs load downwards from the top of each 32 MiB process slot (`coredll.dll` at about `0x01fa0000`), so the range stays below them, and some add-ons load a DLL at a fixed low address (one at `0x00be0000-0x00c01000`), so it starts no lower than the guest image. If the reservation fails, the worker writes `JoLT\memory.txt` with the slot's modules and regions.

- **System calls**: musl is built to call a private gate (`guest/musl/syscall_arch.h`) instead of `svc`. The gate page at `0x013c0000` jumps into the worker, which saves the guest registers on the guest stack and dispatches through a table (`worker/syscalls.c`).
- **fork**: the parent allocates a slot and claims a pre-started spare worker (or starts one). The child copies the parent's worker state and committed guest pages to the same addresses with `ReadProcessMemory`, text from the shared image, then resumes at the saved frame with 0 in `r0`.
- **execve**: the worker releases guest memory and loads the image again in place. Only the qualified BusyBox image can run; `#!` scripts run through an applet.
- **Signals** are delivered when a system call returns: a frame is pushed on the guest stack, the handler runs via a trampoline in the gate page, and musl's restorer calls `rt_sigreturn`. Default stop actions park the worker until `SIGCONT`; interrupted calls restart transparently. CPU-bound processes are terminated by the broker if a fatal signal is not taken within 1.5 s.

## Files

Guest `/` is the `root` folder beside the executables. Paths are normalised and confined in `worker/path.c`. `/bin`, `/dev` and `/proc/self/exe` are virtual. Open file descriptions with shared offsets live in the kernel mapping; each process caches its own CE handle per description. FAT has no permissions or links: files under a `bin` directory or ending `.sh` report as executable, read-only maps to the FAT read-only attribute, and `rename` over an existing file goes through a backup name.

## Terminal

`terminal/display.c` drives libvterm and paints damaged cells with `ExtTextOutW`, in the theme's colours with bold and underline. Lines leaving the screen go to a 400-line scrollback. `terminal/tty.c` implements the canonical and raw line discipline and generates `SIGINT`, `SIGQUIT` and `SIGTSTP` for the foreground process group. The `jolt` command (`rootfs/usr/bin/jolt`) sends private OSC 7770 sequences that `terminal/settings.c` applies and stores in `jolt.cfg`.

## Limits

No threads, named Unix sockets (socketpair only), `mmap` of files, `ptrace`, or dynamic linking. Up to 16 processes, 64 descriptors each, 12 pipes (a socketpair uses two). BusyBox text pages are aliased from the shared image mapping with `VirtualCopy`, so fork and exec do not copy them. Signals are checked only at system call boundaries.
