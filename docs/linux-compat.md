# Native Linux Compatibility Layer For Jornada 720

Status: staged implementation specification, 2026-10-13. JoLT (Jornada Linux Terminal) Gate1/Gate2 and a bounded BusyBox ls/vi filesystem/terminal profile now pass in the emulator and on the physical Jornada, including independently byte-verified vi save/quit. Complete VFS/terminal semantics, shell/process support and Vim remain proposed. This is independent of the bare-metal Linux spikes. The [README](../README.md) and [FEATURES](../FEATURES.md) record the implemented subset, build and known limits; the requirements below are not all completion claims.

The first feasibility prerequisite observed CE application CPSR in System mode (`0x1f`), including after `SetKMode(FALSE)`. The initial strict CPU User-mode gate was an overly strong safety assumption, not a native-execution requirement. With user approval, subsequent trusted probes run in CE's observed ordinary application mode without mode changes or privilege elevation. Page protections and worker-fault containment must be measured; System mode does not establish sandbox isolation. See [compat/probe/README.md](compat/probe/README.md) for results and remaining coverage.

The revised bounded Gate1 probes now pass on the physical Jornada: native low-address execution, ABI canaries, modified-code cache flush, protection/cleanup, unchanged thread-context roundtrip, named-memory/event IPC and contained child fault with CE/RAPI responsive afterward. This is a feasibility result, not an ELF/syscall implementation or hostile-code sandbox. CE reported the faulted child with exit code0, so Linux fault-status translation remains necessary.

## Decision

Build a normal Windows CE 3.0 application that loads a deliberately supported subset of **ARMv4 Linux ELF executables**, runs their instructions natively, and implements their Linux userspace services through CE.

The first release uses a controlled libc/toolchain with a syscall bridge. It is not a claim that arbitrary Debian armel binaries can run unchanged. Compatibility with existing ELF files is a later, separately qualified feature.

The layer must not boot a Linux kernel, take over physical RAM, modify interrupt vectors, change CPU/MMU ownership, or write ROM/EEPROM. Exiting the application returns to the existing CE desktop normally. It must not require kernel-mode memory access or the privileged techniques used by LCDRepair.

**First useful target:** a terminal, BusyBox shell/file tools and an editor operating within a CF workspace. BusyBox `vi` is the initial editor; a reduced Vim build follows only after the process, terminal and memory gates pass.

## Terminology And Scope

- **Native instructions:** guest arithmetic, branches and ordinary memory accesses execute directly on the SA1110, not through an ISA interpreter.
- **Linux ABI:** ELF layout, ARM EABI, syscall numbers/results, guest structures and expected libc interfaces.
- **CE backend:** host allocations, files, events, processes and terminal UI, reached through a bridge. Linux code cannot call coredll as if its calling convention were Linux AAPCS.
- **Controlled executable:** built for the declared instruction/ABI profile, with the layer's syscall backend and manifest. It is still an ELF program, but requires this runtime.
- **Existing executable:** a binary originally built for Linux without this backend. It needs verified syscall-site transformation or a separately proven exception mechanism.

Not in the first release: general distribution compatibility, dynamic glibc, multithreaded Linux applications, loadable modules, privileged syscalls, containers, X11, raw device access or transparent CE hibernation.

## Safety Model

Guest RAM is ordinary CE process memory obtained through allocation APIs. Guest file access is mediated by the virtual filesystem. There are no fixed physical-RAM reads/writes or mapped hardware registers.

Native execution is **not an adversarial sandbox**. A malformed or hostile ELF can attempt arbitrary host-address accesses or branches; this design does not promise the isolation of a full CPU interpreter. BusyBox is checked against the SHA-256 compiled into JoLT-Term, because its applet list and shared code pages depend on it. Other programs must carry the JoLT ABI note (`.note.jolt`, added by the guest crt and linker script) unless `JOLT_ALLOW_UNMARKED=1` is set; the note catches binaries not built for this runtime, it is not a security check. A faulting program takes down only its own worker. Loader validation reduces mistakes, not all possible native-code attacks.

Required behavior:

- Never elevate a worker to kernel mode or grant unrestricted process permissions. Verify the actual worker privilege mode on the target ROM.
- Deny kernel/system syscall families and raw paths such as physical devices, flash, CE's Windows folder and registry.
- Export only the configured CF workspace; read-only by default for an unfamiliar share.
- Do not change CE's storage/program-memory division, suspend policy or drivers automatically.
- On unsupported features, return the correct Linux error or refuse the executable. Do not pretend success.
- On allocation or bridge failure, terminate the affected worker cleanly; do not leave CE globally suspended or locked.
- Back up the CF test workspace during early write/rename testing. Program isolation does not protect files which the user explicitly shares writable.

## Architecture

```text
CE terminal application
  | session control, keyboard, VT output, resource limits
  v
CE supervisor / broker
  | process table, fd objects, pipes, guest paths, signals, wait state
  v
CE worker process
  | ELF loader + Linux-compatible startup + ARM ABI bridge
  v
ARMv4 guest code runs natively
  | controlled syscall gate
  v
Linux ABI dispatcher -> CE operations / broker requests -> guest result
```

Each guest process gets a CE worker, not merely another thread sharing all guest memory. Start with one worker. Multiple workers are enabled only after consistent address placement, fd lifetime and resource accounting are demonstrated.

### Host Modules

| Module | Responsibility |
| --- | --- |
| Supervisor | Sessions, quotas, worker launch/exit, process IDs, cancellation and IPC. |
| Worker loader | Validate ELF/manifest, reserve guest address space, map segments, construct stack/auxv, enter guest. |
| ARM bridge | Save guest context, switch to a host-owned stack, call the dispatcher with CE ABI, return with guest ABI. |
| Syscall dispatcher | Explicit supported-number table, guest pointer validation, structure translation, errno mapping. |
| VFS | Scoped path traversal, UTF-8/UTF-16 conversion, metadata, files, pseudo-devices and mounts. |
| FD objects | Shared open-file descriptions, offsets, reference counts, close-on-exec, pipes and terminal endpoints. |
| Process service | Exec/spawn, wait, limited fork/vfork plan, signal delivery and worker teardown. |
| Terminal | CE window/keyboard integration, VT parser, resize state, canonical/raw input and foreground job state. |
| Guest SDK | Pinned libc, startup objects, ARMv4 runtime helpers, package configs and manifests. |

Reuse the existing Jornada CE LLVM toolchain for host code. Use a distinct Linux ARMv4/EABI toolchain for guest code. Do not mix compiler targets or import libraries between them.

## Execution And Syscall Bridge

### Instruction Profile

ELF32, little-endian ARM, EABI, ARM state only, software floating point, native ARMv4 instructions. Audit all compiler-runtime and third-party objects, not only the application's flags.

Reject Thumb, BX/BLX, CLZ, hardware floating point, ARMv5+ atomics/instructions and unqualified runtime helpers. An `arm-linux-gnueabi` prefix or an `armel` package label is not proof of ARMv4 compatibility.

The guest build pins a libc version/configuration and controls TLS/atomic helpers. It must not execute Linux's fixed-address `__kuser_*` helpers in CE's high-vector/shared-data area. That area cannot be replaced by a fake Linux vector page. Single-thread TLS, if needed by libc, goes through the layer's backend; unsupported helper conventions are refused.

### First Release: Controlled Backend

Build a small static libc with its ARM syscall backend routed to a guest-ABI assembly gate rather than executing `SVC`. All native libraries must use that backend or declare additional audited syscall sites. Raw inline-syscall assembly is out of profile initially.

At startup the worker supplies a versioned gate descriptor to the controlled CRT, using an explicitly documented private auxiliary-vector entry. The descriptor contains no Windows API pointers usable as Linux function pointers; guest code calls the assembly gate using the layer's guest ABI.

Gate context includes syscall number, raw argument words, guest SP/PC, callee-saved registers and per-worker identity. Preserve ARM EABI argument ordering, including alignment/split-word rules for 64-bit offsets. Results use Linux negative errno conventions, not CE `GetLastError` values.

The assembly boundary must preserve the guest's registers as required by its ABI, maintain stack alignment, switch away from the guest stack for host API calls, and avoid ARMv4-incompatible call/return instructions. It must be reentrancy-safe even in the initial single-thread profile: host callbacks cannot overwrite one global saved-context buffer.

Host Win32 callbacks never run directly on the guest stack. File I/O and broker calls may block the worker, not the CE UI thread. No guest pointer is passed unchecked into a Windows API.

### Later: Existing ELF Files

Do not rely on CE delivering Linux `SVC #0` as a catchable user exception. The emulator's host SWI hook is not an API available to a physical CE process.

Candidate approach: load-time transformation of **manifested syscall sites** to ARM branch-without-link veneers, preserving the guest LR, flags and continuation. A one-instruction `BL` replacement is not correct because Linux SVC does not clobber the guest LR. Branch-range limits require verified veneer placement.

No blind scan/replacement of every word resembling SVC: literal pools and data can contain the same bytes. The manifest must identify supported code sites and original instruction bytes, match an executable hash, and reject missing or mismatched sites. Instruction-cache synchronization follows transformation using a proven CE API.

An exception-based scheme is allowed only after physical probes establish complete context capture, continuation and fault containment. It is not the baseline. Self-modifying/JIT code and runtime-generated syscall sequences remain unsupported until separately addressed.

## ELF Loader And Address Space

First profile: static `ET_EXEC`, no `PT_INTERP`, no shared objects or runtime text relocations. Position-independent/dynamic executables are later work.

Validate ELF class, endianness, machine, EABI attributes, segment bounds, overlap, alignment, file offsets and entry point before allocation. Load only declared segments; zero BSS; validate executable entry and page permissions. Use structured ELF parsing, not permissive magic-byte acceptance.

Construct `argc`, `argv`, `envp`, auxv and a guest stack matching Linux EABI. Provide honest values for page size, program headers, entry, capabilities, UID/GID and random data. Do not advertise VFP/Thumb features the hardware lacks or invent entropy.

### CE Slot Collision Gate

CE 3.0 uses process-slot addressing. The existing host PE linker starts code at `0x11000`, overlapping the conventional low Linux ELF layout. A loader cannot simply copy a Linux binary over itself.

Before packages, prove a host worker/bridge layout outside the guest image/heap/stack interval and CE shared modules. Measure valid fixed-address reservations on the actual ROM. Candidate layout: controlled guest image and heap in a bounded low region, host code/stack/bridge in a separately linked region of the same worker slot; exact addresses are probe results, not hard-coded assumptions in this spec.

Historical (ABI version 1): a worker linked at `0x00801000` runs. VirtualQuery describes the low `0x10000` gap as already reserved by the worker image, so a fresh reservation there is rejected. An ordinary commit into the verified unused gap succeeds, and a page executes native ARMv4 code. The loader must distinguish host-owned reservation from guest-owned committed pages and decommit only its own pages on teardown. Subsequent Gate2/BusyBox tests exercise bounded ELF/heap/stack pages within this profile, not every possible allocation or a full-arena stress workload.

Reserve all guest intervals before committing them. Keep host heap allocation out of the guest arena. Guest pointers stay within the worker's approved image/heap/mapping/stack ranges. Kernel/global CE virtual addresses are never guest allocations.

If CE cannot provide repeatable address placement with that layout, stop native ELF work or move to a controlled relocatable guest profile. Do not silently fall back to kernel-mode remapping of CE-owned pages.

`brk` manages the tracked guest heap; anonymous mmap manages guest reservations/commits. Never report an arbitrary host allocation as a guest mapping if its address or semantics differ. Fixed mappings are permitted only within available guest ranges; conflicting mappings fail.

## Filesystem And CF Sharing

Suggested namespace:

```text
/bin, /lib, /etc       controlled root bundle
/home/user            persistent guest workspace
/tmp                  bounded volatile scratch
/mnt/cf               explicitly shared CE directory
/dev                  terminal/null/zero/random pseudo-devices
/proc                 minimal process/runtime information
```

Current host root: `\Storage Card\JoLT\root`, beside the runtime executables but separated from them. Guest `/` maps there. Never expose the CF root or `\Windows`. The namespace and metadata requirements below remain later design work; no raw CF block device is exposed.

The VFS translates guest UTF-8 paths into CE UTF-16, resolves components and virtual symlinks, bounds traversal and verifies the final path remains under its mount. Reject unsupported encoding, embedded NULs, excessive depth and escaping paths. Guest cwd and environment belong to the worker, not CE's application path semantics.

### FAT Semantics Are Not POSIX Semantics

Native CF directory sharing exposes FAT limitations: case-insensitive names, no native Unix permissions/symlinks/hardlinks, different timestamps and possibly weak crash-atomic rename behavior. Report these limitations explicitly for `/mnt/cf`.

For the controlled root/workspace, use a manifest plus a journaled sidecar for supported virtual inode/mode/symlink metadata. The initial version refuses case-only filename collisions and unsupported hardlinks; it is not a complete case-sensitive POSIX filesystem on FAT. If full semantics become necessary, use a private filesystem image and a proven library/backend rather than increasingly complex filename tricks.

BusyBox applet links can be virtual links declared in the root manifest. Translate ARM Linux `stat`/`stat64`/directory records explicitly. Never expose a Windows structure as a Linux one.

For editing, specify and test temp-file creation, flush, rename/replace and recovery behavior. Vim/vi must not believe a durable atomic save occurred when the backend cannot guarantee it. Preserve failed-save originals and surface errors. A guest-private filesystem image must not simultaneously be edited by CE tools.

## Descriptors, Pipes And Processes

Guest fd integers name runtime objects, not CE HANDLE values. An open-file description owns the shared offset and status flags; dup/fork references that object. Per-fd close-on-exec flags are separate. Pipes require bounded queues, EOF/reference semantics, blocking/nonblocking behavior and wakeups.

The inspected coredll export list includes `CreateProcessW`, `CreateThread`, `VirtualAlloc`, `VirtualProtect`, `GetThreadContext`, `SetThreadContext`, `ReadProcessMemory`, `WriteProcessMemory`, `FlushInstructionCache` and `WaitForMultipleObjects`. It did not expose `CreatePipe` or `DuplicateHandle` under those names. Export presence is not proof of the required runtime semantics. Pipe and fd sharing therefore use the broker, not assumed Win32 primitives.

### Incremental Process Contract

| Feature | Required design / limit |
| --- | --- |
| Single executable | One CE worker; initial proof has write/read/file/memory/exit only. |
| Exec | Load a replacement qualified ELF into a controlled worker; preserve PID/fds/cwd/environment as Linux expects. Supervisor observes exactly one logical process exit. |
| Spawn + wait | New worker from executable, explicit fd transfer and lifecycle; basis for initial command launcher. |
| Fork | Later eager copy of tracked guest memory plus saved register/context state into a worker at identical guest VAs. Parent/child returns, shared fd objects and blocked syscalls need real tests. No claim of copy-on-write initially. |
| Vfork | Implemented only with its documented parent-blocking/address-sharing contract, or constrained through the controlled libc/package spawn path. A copy masquerading as fully general vfork is not accepted. |
| Linux threads/clone | Unsupported initially; required clone flags return appropriate errors. Do not emulate by sharing one global runtime context. |
| Wait/zombies | Broker records exit state until reaped; handles repeated wait, WNOHANG and signals without orphaning CE workers. |

BusyBox `ash`, pipelines and arbitrary external commands depend on this contract. They are **not** part of the first single-process ELF success claim. Package configuration may use verified no-fork applets and a controlled spawn backend, but that must be called out as a package adaptation rather than generic Linux compatibility.

Eager fork is expensive on 32 MB: bound worker count, committed memory and total session memory. If fork cannot fit, return ENOMEM correctly. Do not promise general pipelines until they fit and pass on hardware.

## Terminal, Signals And Scheduling

A CE terminal window renders guest text using ordinary GDI, keeping the desktop, power management and LCD drivers owned by CE. Use a bounded scrollback and stable character cells; never touch Epson/MCU registers.

Needed for editors: VT100/ANSI cursor moves, scrolling regions, erase operations, attributes, alternate screen, keyboard escape sequences and resize reports. Add only the sequences required by the tested applications, with explicit unknown-sequence handling.

Guest termios is runtime state, not a cast of CE serial settings. Implement canonical/raw mode, echo, erase, VMIN/VTIME behavior and relevant ioctls. Keep host GUI responsive while workers block on input. Optional serial terminal transport is a separate frontend.

Initial signals are queued for delivery at defined guest bridge checkpoints. Preserve/register Linux handler state and construct validated guest frames for the supported profile. Ctrl-C must interrupt a blocked read or sleeping syscall and produce the declared EINTR/restart behavior.

This is not enough for arbitrary asynchronous signal delivery to CPU-bound guest code. Physical thread-context/exception probes must establish safe preemption before claiming that behavior. Until then, provide a supervisor stop/terminate control and document checkpoint-only delivery. A watchdog terminates a runaway worker without killing the CE UI.

Job control, foreground process groups, SIGCHLD and terminal-generated signals are a shell milestone, not optional details hidden behind a working prompt.

## Initial Syscall Coverage

Pin the syscall/structure matrix to the chosen ARM libc version, including whether it uses legacy time calls or newer time64 interfaces. Implement actual calls observed in reproducible package traces; syscall names alone do not define an ABI.

| Group | Initial coverage and progression |
| --- | --- |
| File basics | open/openat, close, read/write, lseek/_llseek, stat/fstat variants, getdents variants, access, unlink, rename, mkdir/rmdir, truncate, fsync; required subset grows with package tests. |
| Memory | brk, anonymous mmap2, munmap, limited mprotect; private file mappings/caching later. Shared/fixed mappings require their own gate. |
| Identity/startup | exit/exit_group, getpid/ppid, single-user uid/gid reporting, uname, auxv, required ARM-private TLS calls. |
| Time | nanosleep/clock_nanosleep, gettimeofday/clock_gettime and libc-required time64 variants; monotonic versus wall-clock semantics kept distinct. |
| Terminal | read/write, ioctl for tested termios/window-size requests, poll/select and wakeups. |
| Process | exec, spawn/wait first; fork/vfork, fd duplication/sharing, process groups and signals in later milestones. |
| Miscellaneous | fcntl subset, umask and filesystem metadata policy; random source only after a credible CE source is established. |
| Deferred | sockets/DNS, futex/threading, dynamic linker, file-backed shared mmap, advanced signals and Unix sockets. |
| Denied | mount/unmount, ptrace, reboot, kernel modules, raw devices, privilege changes and direct MMIO. |

Validate pointer ranges/overflow, strings, arrays and nested structures before side effects. Partial I/O, EOF, cancellation and interrupted waits must match the declared profile. Maintain an explicit CE-error-to-Linux-errno table, including ENOSYS, ENOTSUP, EFAULT, ENOMEM, EACCES and name/path errors.

## Package Profiles

| Package | Target / gate |
| --- | --- |
| Hello/file probes | Static ARMv4 ELF using the controlled backend; mandatory before BusyBox. |
| BusyBox tools | Start with single-process ls/cat/echo/cp/mv/rm/mkdir/head/tail/grep/sed/awk and vi, as their ABI requirements pass. Disable networking/privileged/mount applets. |
| BusyBox shell | ash is enabled only after tested exec/wait/fd/pipes/signal behavior or a clearly declared adapted spawn/no-fork profile. |
| Vim | Small build, no GUI/Python/Perl/Lua/terminal subprocess support initially; validate terminal, save/rename, memory and signal use. Completion is not implied by vi working. |
| Utilities | diff/find/sort/tar/gzip and similar tools added through deterministic file tests and measured RAM use. |

Do not combine packages into an unbounded all-features image. Keep source versions, cross-toolchain, compiler runtime, feature configs, licenses and ELF manifests reproducible. Distribute applicable source/license material for BusyBox/Vim/libc and any reused loader/runtime code.

## Resource Targets

The device has 32 MB physical RAM shared with CE's storage/program allocation. Free space reported by RAPI is not available program RAM. Query the actual committed/available program memory and CE slot layout before accepting a session.

Planning targets, not measured results:

- One worker first; small binaries and bounded terminal buffers.
- Host supervisor/terminal overhead target below 1 MiB, to be measured rather than guaranteed.
- Guest image/heap/stack budget initially 4 MiB committed per worker; larger programs require an explicit resource profile.
- No hidden fork copy beyond the session cap; start with at most two active workers once processes work.
- CF files are streamed, not read entirely into RAM. File caches, pipe queues and metadata journals are bounded.

Native CPU-heavy work should avoid full ISA-emulation overhead, but syscall, file, terminal and eager-copy overhead must be benchmarked. No instruction-rate or interactive-latency guarantee is made before physical tests.

## Proof Gates And Milestones

1. **CE feasibility probes:** fixed virtual allocation/execute/protect/cache flush; host/guest address separation; normal user privilege; process isolation; exception containment; usable thread context; broker IPC. Run in the emulator and on the exact device ROM without touching physical addresses.
2. **One native ELF:** load a controlled freestanding ARMv4 ELF, set startup state, cross the syscall gate for write/exit, and return safely to CE. Check register/stack/ABI canaries and instruction-cache behavior.
3. **Files and memory:** copy/edit files only inside a disposable CF workspace, exercise error paths, anonymous mappings and resource limits, and verify CE data/settings remain unchanged.
4. **Single-process tools/editor:** qualified BusyBox applets plus vi with working terminal modes and save recovery. Measure memory and responsiveness on hardware.
5. **Processes and shell:** exec/spawn/wait/descriptor sharing/pipes/signals; test shell scripts and pipelines. Fork/vfork get explicit semantics tests before they are advertised.
6. **Vim and useful bundle:** controlled Vim profile, shell/file utility workload, persistence and graceful exit. This is the first full requested environment milestone.
7. **Existing-binary compatibility:** manifested syscall transformation, helper/TLS handling and qualified additional ELF profiles. Dynamic ELF is independent later work, not a checkbox.

Stop or re-scope if ordinary CE allocations cannot provide the guest layout, native exceptions cannot be contained, or shell/editor memory use is unacceptable. A native CE Unix-tool port or small interpreted Linux VM remains an alternative; do not silently mix their compatibility claims with this design.

## Acceptance Tests

- Repeated launch/exit, worker fault and OOM leave CE desktop, brightness/contrast, input, networking and unrelated CF files intact; no reset required.
- Bridge canaries cover ARM registers, LR, flags where required, SP alignment, 64-bit argument ordering, errno and concurrent/callback contexts.
- Malformed ELF/manifests are refused before execution; syscall buffer/path overflows and invalid pointers fail without host side effects.
- CF path traversal, symlink escapes, case-only collisions, readonly mounts and Unicode boundaries have dedicated tests.
- Guest writes survive a clean close/relaunch; interrupted saves/rename failures preserve the prior file or produce an explicit recoverable result.
- cat/grep/sed/awk/diff/find operations compare against Linux reference outputs for the supported options.
- Editor cursor/modes, Ctrl-C, resize and save/quit work without losing guest or host input.
- Pipeline EOF, fd offsets after dup/fork, close-on-exec, child exit/wait, cancellation and resource caps are correct before shell compatibility is claimed.
- Run hardware soak sessions and compare measured program RAM/CF contents before and after, not only emulator screenshots.

## Suggested Repository Layout

Implement as a new `compat/` subtree, not by extending the bare-metal loader:

```text
compat/host/           CE supervisor, worker, terminal and Win32 backend
compat/abi/            guest register/structure/syscall definitions and ARM gates
compat/elf/            validated loader and manifests
compat/vfs/            scoped filesystem, metadata and pseudo-devices
compat/guest/          pinned CRT/libc/package configurations
compat/tools/          image/manifest generation and trace comparison
compat/tests/          ABI, process, filesystem, terminal and hardware fixtures
```

Do not create this whole tree until the feasibility probes pass. Reuse the Jornada SDK/CMake helpers and existing emulator/RAPI tools; keep test state, binaries and disposable CF images under ignored `build/`.

## Verified Basis And Open Questions

Verified locally: the existing SDK emits native ARMv4 CE executables; the ROM export list contains the allocation/process/thread/context/cache APIs listed above; its normal PE linker starts at0x11000; the emulator raises an ordinary ARM SVC exception unless its host-only hook intercepts it. None of these establishes a physical Linux-SVC interception service or fixed-address guest layout.

Measured baseline: page size4096, allocation granularity65536, high-linked host worker/native low-page execution, protection read failure, child fault termination, thread-context unchanged roundtrip and IPC work on this ROM. Gate2 additionally verifies one exact static ARMv4 ELF, startup arguments/auxv, custom-gate write/exit, ENOSYS/EFAULT and return to CE. It exercises sparse pages within the 4 MiB address profile, not a fully committed 4 MiB workload. Open gates: broader ELF/ABI coverage, guest-specific fault reporting/SEH, arbitrary context installation, fork memory/semantics, libc TLS/atomic requirements and complete CE terminal/editor semantics. Resolve those before promising BusyBox shell or Vim.

The failed bare-metal warm-return experiment is the reason for a normal CE-hosted design, not a mechanism to reuse here. The compatibility layer must not load or restore CE bootstrap RAM, call the bare-metal loader, or depend on LCDRepair.