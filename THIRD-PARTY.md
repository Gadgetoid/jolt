# Third-party software

JoLT's own source is MIT licensed (see LICENSE). Release builds include or are built from:

| Component | Version | Licence | Used for |
| --- | --- | --- | --- |
| BusyBox | 1.38.0 | GPL-2.0 | `busybox.elf`: the shell and applets |
| links | 2.30 | GPL-2.0-or-later with OpenSSL exception | `/usr/bin/links`, linked with LibreSSL |
| htop | 3.5.3 | GPL-2.0-or-later | `/usr/bin/htop` |
| frotz | 2.55 | GPL-2.0-or-later | `/usr/bin/frotz`, with `apps/frotz/no-root-check.patch` |
| nano | 9.2 | GPL-3.0-or-later | `/usr/bin/nano` |
| Bash | 5.3 | GPL-3.0-or-later | `/usr/bin/bash` |
| musl | 1.2.5 | MIT | C library linked into every Linux program, with JoLT's patches in `guest/musl` |
| LLVM compiler-rt builtins | 21.1.8 | Apache-2.0 WITH LLVM-exception | Integer and soft-float helpers linked into every Linux program |
| LibreSSL | 4.3.3 | OpenSSL, SSLeay and ISC | TLS for links |
| curl | 8.22.0 | curl (MIT-style) | `/usr/bin/curl` |
| Mbed TLS | 3.6.7 | Apache-2.0 OR GPL-2.0-or-later | TLS for curl |
| DejaVu Sans Mono | 2.37 | Bitstream Vera licence, changes public domain | `/usr/share/fonts/DejaVuSansMono*.ttf` |
| Liberation Mono | 2.1.5 | OFL-1.1 | `/usr/share/fonts/LiberationMono*.ttf` |
| Spleen | 2.2.0 | BSD-2-Clause | `/usr/share/fonts/spleen-*.jbf`, converted from BDF by `tools/bdf2jbf.js` |
| Terminus | 4.49.1 | OFL-1.1 | `/usr/share/fonts/terminus-*.jbf`, converted from BDF by `tools/bdf2jbf.js` |
| Colour themes: Solarized, Gruvbox, One, Dracula, Nord | | MIT (each) | Palettes in `/usr/share/themes`, by Ethan Schoonover, Pavel Pertsev, GitHub's Atom team, Zeno Rocha and Arctic Ice Studio |
| Mozilla CA certificates (via curl.se) | 2026-09-25 | MPL-2.0 | `/etc/ssl/cert.pem`, trusted roots for curl and links |
| dvtm | 0.15 | MIT | `/usr/bin/dvtm`, with `apps/dvtm/narrow-curses.patch` |
| sfeed | 2.4 | ISC | `/usr/bin/sfeed*` |
| ncurses | 6.6 | X11 (MIT-style) | Terminal library linked into Vim, nano, Bash, ncdu, htop, frotz, dvtm and sfeed_curses |
| Vim | 9.2.1178 | Vim licence | `/usr/bin/vim` |
| Lua | 5.4.7 | MIT | `/usr/bin/lua` |
| MicroPython | 1.29.0 | MIT | `/usr/bin/micropython` |
| jq | 1.8.2 | MIT | `/usr/bin/jq` |
| ncdu | 1.22 | MIT | `/usr/bin/ncdu` |
| Dropbear | 2026.94 | MIT-style | `/usr/bin/dbclient`, `/usr/bin/dropbearkey` |
| Zork I, II, III | historicalsource | MIT | Story files in `/usr/share/games/zork` |
| libvterm | 0.3.3 | MIT | Terminal emulation in `JoLT-Term.exe` |
| crypto-algorithms (Brad Conte) | cfbde48 | Public domain | SHA-256 in `JoLT-Term.exe` and `JoltWorker.exe` |

Each release has a `gpl-sources` archive with the complete corresponding source of the GPL programs above: their upstream archives, the musl, compiler-rt, ncurses and LibreSSL archives they are statically linked with, `busybox.config` and the frotz patch. The build scripts are this repository's `guest/`, `apps/` and `tools/` directories. `tools/fetch.sh` lists every upstream URL and SHA-256 checksum, including the permissively licensed components, which are unmodified.
