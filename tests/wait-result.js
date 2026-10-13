const { execFile } = require("child_process");
const [tool, connection, remote, local, limit] = process.argv.slice(2);
let finished = false;
const deadline = setTimeout(() => {
    finished = true;
    console.error("JoLT result deadline expired");
    process.exitCode = 1;
}, Number(limit || 25000));
function check() {
    if (finished) return;
    execFile(tool, [connection, "--timeout=5", "get", remote, local], (error) => {
        if (finished) return;
        if (!error) {
            finished = true;
            clearTimeout(deadline);
        } else setTimeout(check, 1000);
    });
}
check();