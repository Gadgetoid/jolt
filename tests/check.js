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
    font: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["bad font 1", "\r\n15 102\r\n", "\r\n11 77\r\n"] },
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
    vim: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["vi improved 9.2", "\r\nxterm-256color\r\n", "\r\nllo from vim\r\n"] },
    shortcut: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["55#\"\\storage card\\jolt\\jolt-term.exe\" -e \"echo hi; ssh -v\"", "43#\"\\storage card\\jolt\\jolt-term.exe\" -e \"top\"", "bad 1", "dropbear v2026"] },
    perf: { result: [], tty: [] },
    launch: { result: ["exit=3 signal=0"], tty: ["\r\nlaunched with args\r\n"] },
    hold: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\nheld command\r\n", "\r\nin shell after hold\r\n"] },
    "ssh-probe": { result: [], tty: [] },
    serial: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["4,  64", "4,  67", "'/dev/ttys0': resource busy", "'/dev/ttys1': no such device", "speed 115200 baud", "\r\nhello serial\r\n", "\r\nmicro\r", "microcom status 0"] },
    apps2: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n[2,4,6]\r\n", "mpy micropython 45", "[0, 1, 4, 9, 16]", "gnu nano, version 9.2", "ncdu 1.22", "htop 3.5.3", "frotz v2.55", "zork1.z3", "os: jolt", "windows ce 3.00", "host: hp jornada 720", "packages: "] },
    netapps: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\n32\r\n", "\r\n1\r\n", "jolt links test", "rendered in text mode."] },
    vi: { result: ["JOLT_SHELL PASS exit=0 signal=0"], tty: ["\r\njolt edited this file.\r\n"] },
};
const expected = expectations[name];
if (!expected) throw Error("No expectations for " + name);
for (const marker of expected.result) if (!result.includes(marker)) throw Error(name + ": missing result " + marker);
for (const marker of expected.tty) if (!tty.includes(marker.toLowerCase())) throw Error(name + ": missing output " + JSON.stringify(marker));
console.log("PASS:", name);
