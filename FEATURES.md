# JoLT - Jornada Linux Terminal

## Required

Startup splash into a usable terminal/shell, "exit" exits back to desktop - DONE
A nice ASCII welcome message - DONE
A custom "help" command explaining how to use the system - DONE (as `jolt help`)
Busybox, vim
Which shell should we use?
NETWORK SUPPORT??

## Wishlist

### More BusyBox applets (config only) - DONE (162 applets incl. usage text, full find/grep/xargs/less options; bzip2 needs -1 to -6, memory)

- Archives: tar, gzip/gunzip, bzip2, unzip
- Inspection: od, xxd, strings, base64, du
- Text: nl, paste, fold, rev, comm, cal
- Misc: bc/dc, time, watch, patch, ed, free, uptime
- df (needs statfs) - DONE
- ps, top, pstree (needs a synthetic /proc from the broker process table) - DONE

### Run more than one binary

- Loader runs ELF programs other than busybox.elf - DONE (JoLT ABI note instead of a hash manifest; JOLT_ALLOW_UNMARKED=1 overrides)
- Lua - DONE (5.4.7)
- nano - DONE (9.2)
- mg or kilo
- jq - DONE (1.8.2)
- ncdu - DONE (1.22)
- htop - DONE (3.5.3)
- joltfetch, a neofetch for JoLT - DONE (/proc/jolt carries the CE details)
- sqlite3 shell
- frotz - DONE (2.55, with Zork I-III under MIT as `zork [1|2|3]`)
- vim (tiny feature set, builtin termcap) - DONE (Vim 9.2 tiny over ncurses 6.6; normal needs a larger image limit)
- MicroPython UNIX port - DONE (1.29; no threads, FFI or SSL)
- ping and traceroute via CE IcmpSendEcho (WsControl) behind emulated raw ICMP sockets
- SQLite as a shared library linked into Lua (lsqlite3) and MicroPython (usqlite), plus the sqlite3 shell; no WAL/mmap, threads off, fcntl locks are a no-op today
- NetHack

### Serial

- /dev/ttyS0 backed by CE COM1:, termios baud/parity mapped to DCB - DONE (/dev/ttyS0-3 = COM1:-COM4:, JoLT-Term owns the ports; raw, no line discipline; data path untested until the emulator exposes COM4)
- BusyBox microcom - DONE
- Shell on a serial port (needs the line discipline on serial devices)
- picocom

### Networking

- Sockets backed by Winsock (TCP, UDP, select/poll), /etc/resolv.conf - DONE (sockets owned by JoLT-Term, DNS via CE)
- BusyBox nc, wget, telnet, ftpget/ftpput, httpd - DONE (plus tftp, whois, nslookup)
- Dropbear dbclient (ssh) - DONE (dbclient, dropbearkey; CryptoAPI randomness)
- HTTPS in wget - DONE (BusyBox TLS 1.2 via ssl_client helper over socketpair; no certificate validation)
- BearSSL for HTTPS in links and other apps
- links (text mode browser) - DONE (HTTP only until BearSSL)

### Terminal

- Colour and bold rendering - DONE (classic and Solarized themes)
- ncurses with a minimal terminfo entry - DONE (ncurses 6.6, built-in fallbacks, TERM=xterm-256color)
- ptys (/dev/ptmx, /dev/pts/*)
- dvtm + abduco (multiplexer)

### Audio

- Terminal bell (`\a`) - DONE (MessageBeep)
- PC speaker tier: KIOCSOUND/KDMKTONE ioctls on the tty synthesised as a square wave through waveOut, BusyBox beep - DONE (tone stops when its owner exits)
- /dev/dsp (OSS): write() PCM into waveOut buffers, SNDCTL_DSP_SPEED/SETFMT/CHANNELS mapped to WAVEFORMATEX
- Players on top of /dev/dsp (e.g. a tracker or mp3 decoder, CPU permitting)

### Pocket Outlook data

- `jolt sync` (or similar): export CE databases (Contacts, Calendar, Tasks, Inbox via CEDB) read-only as CSV/JSON for jq, Lua or Python; maybe write back later

### Files

- `jolt mount "/real path" /mountpoint`: map CE folders into the guest tree (DOSBox-style) - DONE (mount -t cefs, /etc/fstab, /proc/mounts)

### CE integration

- /dev/clipboard backed by the CE clipboard - DONE (UTF-8 and LF in Linux, UTF-16 and CRLF on CE)
- Battery status - DONE (/proc/apm, `jolt battery`)
- `jolt open FILE` with the associated CE application - DONE (ShellExecuteEx)
- JoLT icon - DONE (from the splash bolt)
- Shortcuts that run a command - DONE (JoLT-Term.exe [--hold] -e "COMMAND", `jolt shortcut [-s] NAME COMMAND`)
- More than one JoLT window at a time (single-instance guard today; a second launch focuses the first)

### Known issues

- Vim: deleting does not remove characters, other display quirks - FIXED (TERM=xterm-256color, keys encoded by libvterm, terminal query replies)
- Trailing slash is dropped in path lookup, so `file/` resolves as `file` - FIXED

### Performance

- Process start: BusyBox text aliased from the shared image with VirtualCopy (no copy on fork or exec), batched commit/decommit, AT_RANDOM from a shared entropy pool instead of CryptoAPI per process. About 4x faster `ls` in the emulator (100 ms to 26 ms).
- Next: fork setup (spare hand-off, ~20 ms), apps other than BusyBox still copy their text

## Possible?

Tiny Core Linux style design? with expansion apps shipped somewhat like .tcz? - Or could `/Storage Card/JoLT/root/usr/bin` just contain runnable user apps with no need for containers?

How would installing packages work? apt? our own index somewhere. Hosted in GitHub pages?

## Moon shots?

X server for running simple windowed apps, or a even full lite desktop?
