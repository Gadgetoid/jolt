JoLT - Jornada Linux Terminal

A terminal for the HP Jornada 720 (Windows CE 3.0) that runs BusyBox ash and
its applets as native ARM Linux programs, alongside Windows CE.

Install
  Copy the JoLT folder to the root of your CompactFlash card, so you have
  \Storage Card\JoLT\JoLT-Term.exe. Run JoLT-Term.exe.

Use
  The JoLT folder's "root" subfolder is the Linux filesystem: / in the shell.
  Type help for shell built-ins and jolt help for display settings
  (full screen, dark mode, font size). Type exit to close JoLT.
  \My Documents appears as /mnt/docs. jolt mount maps other CE folders,
  jolt open opens a file in its CE application, and /dev/clipboard is the
  CE clipboard. jolt shortcut NAME COMMAND adds a desktop shortcut that
  opens JoLT and runs COMMAND, e.g. an ssh session. /dev/ttyS0 is the
  serial cable (COM1:) when no PPP or ActiveSync connection is using it.

Notes
  This is an experimental compatibility layer, not a Linux kernel. It runs
  only the BusyBox build shipped with it. Keep copies of important files.
  LICENSE.txt and THIRD-PARTY.txt cover the licences.
