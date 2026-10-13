const fs = require("fs");
const log = process.env.JOLT_LOG;
const watcher = fs.watch(log, check);
const deadline = setTimeout(() => {
    watcher.close();
    console.error("CE desktop handshake did not become ready");
    process.exitCode = 1;
}, 30000);
function check() {
    if (!fs.existsSync(process.env.JOLT_SOCKET)) return;
    if (!fs.readFileSync(log, "utf8").includes("desktop: Handheld_PC, Windows CE 3.x")) return;
    watcher.close();
    clearTimeout(deadline);
}
check();
