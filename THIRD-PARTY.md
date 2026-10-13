# Third-party software

JoLT's own source is MIT licensed (see LICENSE). Release builds include or are built from:

| Component | Version | Licence | Used for |
| --- | --- | --- | --- |
| BusyBox | 1.38.0 | GPL-2.0 | `busybox.elf`: the shell and applets |
| musl | 1.2.5 | MIT | C library linked into `busybox.elf`, with JoLT's patches in `guest/musl` |
| LLVM compiler-rt builtins | 21.1.8 | Apache-2.0 WITH LLVM-exception | Integer and soft-float helpers linked into `busybox.elf` |
| libvterm | 0.3.3 | MIT | Terminal emulation in `JoLT-Term.exe` |
| crypto-algorithms (Brad Conte) | cfbde48 | Public domain | SHA-256 image check in `JoLT-Term.exe` |

`busybox.elf` is distributed under GPL-2.0. Its complete corresponding source is the BusyBox, musl and compiler-rt archives in each release's `third-party-sources` archive, plus this repository's `guest/` directory and `guest/busybox/busybox.config`. `tools/fetch.sh` lists the upstream URLs and SHA-256 checksums.
