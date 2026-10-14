const net = require("net");
const port = Number(process.argv[2]);
const shellMarker = "SHELL-MODE";
const shellPrompt = "# ";
const shellCommands = ["echo serial-$((6*7)) $(tty) > /tmp/serial.out", "stty -a | grep -o onlcr >> /tmp/serial.out", "exit"];
const commandDelay = 300;

function connect() {
    const socket = net.connect(port, "127.0.0.1");
    let received = "";
    let shell = false;
    let next = 0;
    socket.on("data", data => {
        const text = data.toString("latin1");
        received = (received + text).slice(-64);
        if (!shell) {
            socket.write(text.toUpperCase(), "latin1");
            if (received.includes(shellMarker)) {
                shell = true;
                received = "";
            }
            return;
        }
        if (next < shellCommands.length && received.includes(shellPrompt)) {
            received = "";
            const command = shellCommands[next++];
            setTimeout(() => socket.write(command + "\r", "latin1"), commandDelay);
        }
    });
    socket.on("error", () => {});
    socket.on("close", () => setTimeout(connect, 500));
}

connect();
