const fs = require("fs");
const crypto = require("crypto");
const image = fs.readFileSync(process.argv[2]);
if (!image.subarray(0, 4).equals(Buffer.from([0x7f, 0x45, 0x4c, 0x46])) ||
    image[4] !== 1 || image[5] !== 1 || image.readUInt16LE(16) !== 2 ||
    image.readUInt16LE(18) !== 40 || image.length > 2097152)
    throw Error("Guest is not a qualified ARM ELF32 executable");
const applets = process.argv[4] ? fs.readFileSync(process.argv[4], "utf8").split("\n").filter(Boolean) : [];
for (const applet of applets) if (!/^[a-z0-9_.[\]-]+$/.test(applet)) throw Error("Invalid applet name: " + applet);
const digest = crypto.createHash("sha256").update(image).digest();
const header = "#pragma once\n#define JOLT_GUEST_SIZE " + image.length +
    "\nstatic const unsigned char jolt_guest_sha256[32] = {\n    " +
    [...digest].map(value => "0x" + value.toString(16).padStart(2, "0")).join(", ") + "\n};\n" +
    "static const char jolt_applet_names[] = \"" + applets.map(name => name + "\\0").join("") + "\";\n";
fs.writeFileSync(process.argv[3], header);
console.log("Guest SHA256:", digest.toString("hex"), "bytes:", image.length, "applets:", applets.length);
