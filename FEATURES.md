# JoLT - Jornada Linux Terminal

## Done

### Basics

- Startup splash into a usable terminal and shell; `exit` returns to the desktop
- ASCII welcome message
- `jolt help` explaining how to use the system
- BusyBox ash as the login shell, with history saved between sessions

### BusyBox

- 162 applets, including usage text and full find, grep, xargs and less options; bzip2 needs -1 to -6 for memory
- Archives: tar, gzip/gunzip, bzip2, unzip
- Inspection: od, xxd, strings, base64, du
- Text: nl, paste, fold, rev, comm, cal
- Misc: bc/dc, time, watch, patch, ed, free, uptime
- df (statfs)
- ps, top, pstree (synthetic /proc from the broker process table)
- script (ptys)

### Programs

- Loader runs ELF programs other than busybox.elf (JoLT ABI note instead of a hash manifest; JOLT_ALLOW_UNMARKED=1 overrides)
- Lua 5.4.7
- nano 9.2
- bash 5.3 (optional; ash stays the login shell; /etc/bash.bashrc with completion for `jolt`)
- jq 1.8.2
- ncdu 1.22
- htop 3.5.3
- joltfetch, a neofetch for JoLT (/proc/jolt carries the CE details)
- frotz 2.55, with Zork I-III under MIT as `zork [1|2|3]`
- Vim 9.2 tiny over ncurses 6.6
- MicroPython 1.29 UNIX port (no threads, FFI or SSL)
- dvtm 0.15 (narrow ncurses; dvtm and dvtm-256color terminfo built into the ncurses fallbacks)

### Serial

- /dev/ttyS0-3 backed by CE COM1:-COM4:, termios baud and parity mapped to DCB; JoLT-Term owns the ports; raw, no line discipline
- Output drains before a port closes
- BusyBox microcom
- `jolt serial-shell`: BusyBox script relays a serial port to a pty running a login shell

### Networking

- Sockets backed by Winsock (TCP, UDP, select/poll), owned by JoLT-Term; DNS via CE, /etc/resolv.conf
- BusyBox nc, wget, telnet, ftpget/ftpput, httpd, tftp, whois, nslookup, ntpd
- ping and traceroute (-I) through CE's ICMP echo (WsControl) behind emulated raw ICMP sockets; multi-hop untested in the emulator
- Dropbear dbclient (ssh) and dropbearkey, CryptoAPI randomness
- HTTPS in wget (BusyBox TLS 1.2 via ssl_client over socketpair; no certificate validation)
- HTTPS in links (LibreSSL) and curl (Mbed TLS), verified against the Mozilla CA bundle in /etc/ssl/cert.pem
- links text-mode browser

### Terminal

- Colour and bold rendering; classic theme built in, others as kitty-format files in /usr/share/themes (Solarized, Gruvbox, One, Dracula, Nord), dark and light pairs
- Font choice: TrueType from /usr/share/fonts via AddFontResource (DejaVu Sans Mono, Liberation Mono) or CE fonts by name
- Bitmap fonts drawn by JoLT: Spleen (5x8 to 12x24) and Terminus (6x12 to 12x24), BDF converted at build time to 1-bit glyph sheets (.jbf), one BitBlt per cell, Courier New for missing glyphs
- Ctrl+Shift+F toggles full screen
- ncurses with built-in terminfo fallbacks, TERM=xterm-256color
- ptys: /dev/ptmx and /dev/pts/0-3; line discipline shared by the console and ptys (common/tty.c); controlling terminals, TIOCSCTTY, hangup on master close; /proc/PID/fd links
- HP hot keys run commands or terminal settings (`@toggle`, `@font+`) from /etc/hotkeys

### Audio

- Terminal bell (`\a`) via MessageBeep
- PC speaker: KIOCSOUND/KDMKTONE on the tty synthesised as a square wave through waveOut, BusyBox beep; the tone stops when its owner exits

### Files

- `jolt mount`: map CE folders into the guest tree (mount -t cefs, /etc/fstab, /proc/mounts)

### CE integration

- /dev/clipboard backed by the CE clipboard (UTF-8 and LF in Linux, UTF-16 and CRLF on CE)
- Battery status (/proc/apm, `jolt battery`)
- `jolt open FILE` with the associated CE application (ShellExecuteEx)
- JoLT icon, from the splash bolt
- Shortcuts that run a command (JoLT-Term.exe [--hold] -e "COMMAND", `jolt shortcut [-s] NAME COMMAND`)

### Performance

- BusyBox text aliased from the shared image with VirtualCopy (no copy on fork or exec), batched commit and decommit, AT_RANDOM from a shared entropy pool instead of CryptoAPI per process. About 4x faster `ls` in the emulator (100 ms to 26 ms).
- App images cached in named shared mappings keyed by path, size and mtime, with text aliased by VirtualCopy while any process uses them. Nested `bash -c :` 3x faster in the emulator. VirtualCopy needs source and destination at the same offset within 64 KB, so the ELF starts at the view base.

- Recently used app images stay loaded after their last process exits: workers hand new image mappings to JoLT-Term, which holds up to 8 (4 MB, least recently used first) and lets them go when free memory drops below 2 MB. Repeat `vim` starts 160 ms to 60 ms in the emulator
- Foreground first: workers outside the active terminal's foreground (the terminal that last got input, plus the console's foreground) drop to THREAD_PRIORITY_BELOW_NORMAL, checked on each syscall
- Drawing through a 16-bit back-buffer canvas: a shadow grid redraws only changed cells, scrolls and history paging move canvas pixels, bitmap glyphs expand through cached 8-pixel tables per colour pair, one blit per paint; output floods drain in 30 ms batches with a paint at most every 33 ms

### Fixed issues

- Vim: deleting did not remove characters, other display quirks (TERM=xterm-256color, keys encoded by libvterm, terminal query replies)
- Trailing slash was dropped in path lookup, so `file/` resolved as `file`
- dvtm missed resizes (pselect ignored its signal mask)
- Command substitutions with pipelines (`$(echo a | cat)`) sometimes came back empty: the supervisor kept a stale handle when a process slot was reused, saw the old worker's exit and closed the new process's files

## Wishlist

### Programs

- mg or kilo
- SQLite as a shared library linked into Lua (lsqlite3) and MicroPython (usqlite), plus the sqlite3 shell; no WAL or mmap, threads off, fcntl locks are a no-op today
- NetHack
- Vim normal build with sh, Python and Lua syntax (the image limit is raised now)
- picocom
- abduco (needs AF_UNIX sockets bound to paths)

### Terminal

- Windows key: Win+key shortcuts, a second bank of hot keys, or an on-screen overlay (memory, programs, theme, font)
- More than one JoLT window at a time (single-instance guard today; a second launch focuses the first)

### Audio

- /dev/dsp (OSS): write() PCM into waveOut buffers, SNDCTL_DSP_SPEED/SETFMT/CHANNELS mapped to WAVEFORMATEX
- Players on top of /dev/dsp (a tracker or mp3 decoder, CPU permitting)

### Pocket Outlook data

- `jolt sync` (or similar): export CE databases (Contacts, Calendar, Tasks, Inbox via CEDB) read-only as CSV or JSON for jq, Lua or Python; maybe write back later

### Performance and robustness

- Faster fork setup (spare hand-off, about 20 ms)
- Workers exit when JoLT-Term dies; a watchdog

### SHARP Brain support

Reports from Brain users (CE 6.0, ARM926EJ-S):

- JoLT-Term fails to launch silently: the Brain's coredll lacks CryptAcquireContextW, CryptGenRandom and CryptReleaseContext. Both callers (terminal/broker.c, worker/random.c) already fall back without CryptoAPI, so resolving these with GetProcAddress should fix it
- With the exports patched into coredll it reaches the splash, then crashes. Cause unknown; CE 6.0's memory and process model differs from CE 3.0's, so VirtualCopy, shared mappings and cross-process access are suspects
- CPU should be fine: ARMv4 code runs on the ARM926EJ-S
- Untested: screen size, keyboard, hot keys; /proc/cpuinfo is hardcoded to StrongARM-1110

### Possible?

- Tiny Core Linux style expansion apps, like .tcz? Or could `/Storage Card/JoLT/root/usr/bin` just hold runnable user apps with no need for containers?
- Installing packages: our own index somewhere, hosted on GitHub Pages?

### Moon shots

- X server for simple windowed apps, or even a full lite desktop
