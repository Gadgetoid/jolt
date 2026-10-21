const fs = require("fs");
const [name, resultPath, ttyPath] = process.argv.slice(2);
const result = fs.readFileSync(resultPath, "latin1");
const tty = fs.existsSync(ttyPath) ? fs.readFileSync(ttyPath, "latin1").toLowerCase() : "";
const appletCount = fs.readFileSync("build/guest/applets.txt", "utf8").split("\n").filter(Boolean).length;
const expectations = {
    smoke: { result: ["JOLT_SHELL PASS exit=3 signal=0", "splash=1"], tty: ["jornada linux terminal", "\r\nhello from ash\r\n", "welcome.txt"] },
    shell: {
        result: ["JOLT_SHELL PASS exit=0 signal=0"],
        tty: ["\r\n3\r\n", "\r\nbeta\r\n", "got sub", "\r\nn3\r\n", "\r\n/tmp\r\n", "\r\n/etc\r\n", "waited 0",
            "script ran argument", "\r\nfirst\r\n", "200 199 198", "\r\n2\r\n", `\r\n${appletCount}\r\n`, "status 1"],
    },
    interrupt: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["after interrupt 130"] },
    spawn: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\nspawned all\r\n"] },
    display: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["usage: jolt command", "help status 0", "bogus status 1", "\r\n15 79\r\n", "\r\n11 77\r\n"] },
    "fullscreen-exit": { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: [] },
    font: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["bad font 1", "\r\n15 102\r\n", "\r\n11 77\r\n", "\r\n10 61\r\n", "\r\n13 88\r\n"] },
    colour: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["^[[", "theme status 1", "\x1b[1;31mbold red"] },
    jobs: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["stopped", "fg status 0", "(tty input)", "background finished 0"] },
    apps: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["tar ok", "\r\n300\r\n", "ahpla ateb ammag", "\r\nhello\r\n",
        " 61 62 63", "7879", "3\tgamma", "january 2026", "\r\n1048576\r\n", "\r\n42\r\n", "/tmp/b", "rootfs", "total",
        "\r\n1\r\n", "sh", "name:\tcat", "real"] },
    lua: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["lua lua 5.4\t42\tababab", "sum\t20000", "\r\nwritten by lua\r\n", "time\ttrue\t4", "script argument", "\r\njolt\r\n", "/tmp/unmarked was not built for jolt", "unmarked status 126", "\r\nlua 5.4.7  copyright"] },
    network: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\nserved by busybox httpd\r\n", "\r\n24\r\n", "\r\nthrough netcat\r\n"] },
    extras: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["killall 0", "timeout status 143", "360: 2 2 2 3 3 5", "\r\n10\r\n", "e7c22b99", "a  \\n   b  \\n", "2 directories", "nice ok", "\r\nhistory ok"] },
    qol: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\nx.txt\r\ny.txt\r\n", "\r\n2\r\n", "l2 match l4 ", "size 2.0M", "\r\nSHOUT\r\n", "a/x.txt: OK", "\r\nrandom ok", "math 1099511627776", "\r\n/tmp/q/a\r\n", "beep 0", "\r\n3\r\n", "\r\n/usr/bin/ /usr/share/ \r\n"] },
    desktop: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\\my documents on /mnt/docs", "\r\n2\r\n", "\r\nhello\r\n", "\r\n/mnt/win\r\n", "\r\nclip from jolt\r\n", "\r\nmore\r\n", "10, 240", "1.16 1.2 0x03"] },
    vim: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["vi improved 9.2", "\r\nxterm-256color\r\n", "\r\nllo from vim\r\n", "scrolloff=5"] },
    shortcut: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["55#\"\\storage card\\jolt\\jolt-term.exe\" -e \"echo hi; ssh -v\"", "43#\"\\storage card\\jolt\\jolt-term.exe\" -e \"top\"", "bad 1", "dropbear v2026"] },
    perf: { result: [], tty: [] },
    "bash-perf": { result: [], tty: [] },
    flood: { result: [], tty: [] },
    dvtm: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["inside-42", "htop-status-0", "after dvtm 0", "\r\n0\r\n"] },
    "serial-shell": { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["serial shell status 0", "serial-42 /dev/pts/0", "\r\nonlcr\r\n"] },
    "dvtm-resize": { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["20 106"] },
    "bitmap-font": { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["spleen (bitmap: 8 12 16 24)", "terminus (bitmap: 12 14 16 18 20 24)", "\r\n23 123\r\n", "\r\n11 77\r\n", "\r\n7 51\r\n", "\r\n13 77\r\n", "usage: jolt font N", "font status 1", "    terminus (bitmap: 12 14 16 18 20 24)"] },
    subst: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["empty 0 of 60"] },
    theme: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["classic (built in, default)", "gruvbox (dark and light)", "dracula\r\n", "jolt: no theme called nosuch", "theme status 1"] },
    pty: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["/dev/pts/0", "pty-hello", "bg-done", "line a", "line b", "after pty interrupt", "\r\n0\r\n", "\r\n/dev/tty\r\n"] },
    launch: { result: ["exit=3 signal=0"], tty: ["\r\nlaunched with args\r\n"] },
    hold: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\nheld command\r\n", "\r\nin shell after hold\r\n"] },
    "ssh-probe": { result: [], tty: [] },
    serial: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["4,  64", "4,  67", "'/dev/ttys0': resource busy", "'/dev/ttys1': no such device", "speed 115200 baud", "\r\nhello serial\r\n", "\r\nmicro\r", "microcom status 0"] },
    ping: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["64 bytes from 10.0.2.2: seq=2", "3 packets transmitted, 3 packets received", "unreachable status 1", " 1  10.0.2.2 (10.0.2.2)"] },
    https: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n2026\r\n", "curl 200 verify 0", "<title>example domain</title>", "selfsigned status 60", "this domain is for use in documentation"] },
    ntp: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n2000\r\n", "ntpd status 0", "\r\n2026\r\n"] },
    apps2: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n[2,4,6]\r\n", "mpy micropython 45", "[0, 1, 4, 9, 16]", "gnu nano, version 9.2", "ncdu 1.22", "htop 3.5.3", "frotz v2.55", "zork1.z3", "os: jolt", "ce_version: 3.00", "host: hp jornada 720", "packages: "] },
    bash: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["gnu bash, version 5.3", "assoc blue 3", "sum 55 nested deep", "first a", "status 0 0", "func arg", "subshell 3", "inner 5", "background done", "trapped", "script 2 x y", "running", "interactive 5.3", "histsize 500", "ll is aliased", "GNU Bash", "complete theme", "complete DejaVuSansMono\r\n", "\r\n1\r\n"] },
    sfeed: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["update 0", "jolt feed entry", "\r\n   feed body\r\n", "\r\npiped\r\n", "\r\nexecuted\r\n", "\r\npopened\r\n", "html 1", "gophermaps index local other"] },
    "vim-typing": { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["written"] },
    jpm: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n1\r\n", "jpm: 3 packages available", "beta                 test beta", "* alpha 1-1", "jpm: checking alpha 1-1", "jpm: installing alpha (1 file)", "jpm: installed alpha 1-1", "jpm: installed beta 1-1", "install 0", "alpha says 1-1", "beta says 1-1", "/usr/bin/beta", "jpm: beta needs alpha", "remove-needed 1", "gamma is for jolt9, this jolt runs jolt2", "wrong-arch 1", "jpm: installed alpha 2-1", "alpha says 2-1", "jpm: alpha is intact", "jpm: beta is intact", "verify 0", "jpm: alpha has a changed /usr/bin/alpha", "verify-changed 1", "jpm: alpha 2-1 is newer than 1-9 in the repository", "jpm: everything is up to date", "jpm: kept your changes to /usr/bin/alpha, the new version is /usr/bin/alpha.jpmnew", "jpm: installed alpha 3-1", "is damaged or has changed", "bad-sha 1", "/usr/bin/alpha belongs to alpha", "conflict 1", "traversal 1", "rooted 1", "jpm: kept /usr/bin/alpha, it was changed", "jpm: removed alpha", "removed 0", "the card is full", "full 1", "orphan 1", "usage: jpm command", "usage 1", "leftovers 0"] },
    netapps: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n32\r\n", "\r\n1\r\n", "jolt links test", "rendered in text mode."] },
    vi: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\njolt edited this file.\r\n"] },
};
const expected = expectations[name];
if (!expected) throw Error("No expectations for " + name);
for (const marker of expected.result) if (!result.includes(marker)) throw Error(name + ": missing result " + marker);
for (const marker of expected.tty) if (!tty.includes(marker.toLowerCase())) throw Error(name + ": missing output " + JSON.stringify(marker));
console.log("PASS:", name);
