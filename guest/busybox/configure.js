const fs = require("fs");
const configuration = process.argv[2];
const seed = fs.readFileSync(process.argv[3], "utf8");
let text = fs.readFileSync(configuration, "utf8");
for (const line of seed.trim().split("\n")) {
    const match = /^(CONFIG_[A-Z0-9_]+)=(y|n|[0-9]+|"[A-Za-z0-9_ /.-]*")$/.exec(line);
    if (!match) throw Error("Invalid seed: " + line);
    const pattern = new RegExp("^(?:" + match[1] + "=.*|# " + match[1] + " is not set)$", "m");
    if (!pattern.test(text)) throw Error("Unknown BusyBox setting: " + match[1]);
    text = text.replace(pattern, match[2] === "n" ? "# " + match[1] + " is not set" : line);
}
fs.writeFileSync(configuration, text);