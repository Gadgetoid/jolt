const fs = require("fs");
const text = fs.readFileSync(process.argv[2], "utf8");
const block = /applet_names\[\][^=]*=([\s\S]*?);/.exec(text)[1];
const names = [...block.matchAll(/^"([^"]+)" "\\0"$/gm)].map(match => match[1]);
fs.writeFileSync(process.argv[3], names.join("\n") + "\n");
