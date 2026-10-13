const net = require("net");
const port = Number(process.argv[2]);

function connect() {
    const socket = net.connect(port, "127.0.0.1");
    socket.on("data", data => socket.write(data.toString("latin1").toUpperCase(), "latin1"));
    socket.on("error", () => {});
    socket.on("close", () => setTimeout(connect, 500));
}

connect();
