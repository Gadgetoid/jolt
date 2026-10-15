const fs = require("fs");

const MAGIC = "JBF1";
const ATLAS_COLUMNS = 16;
const HEADER_SIZE = 16;
const CODEPOINT_LIMIT = 0xffff;

function readFont(path) {
    const lines = fs.readFileSync(path, "latin1").split(/\r?\n/);
    const font = { width: 0, height: 0, ascent: 0, descent: 0, offsetX: 0, glyphs: [] };
    let glyph = null;
    let bitmap = null;
    for (const line of lines) {
        const [keyword, ...values] = line.trim().split(/\s+/);
        const numbers = values.map(Number);
        if (bitmap && keyword !== "ENDCHAR") {
            bitmap.push(parseInt(keyword, 16));
            continue;
        }
        if (keyword === "FONTBOUNDINGBOX") [font.width, , font.offsetX] = numbers;
        else if (keyword === "FONT_ASCENT") font.ascent = numbers[0];
        else if (keyword === "FONT_DESCENT") font.descent = numbers[0];
        else if (keyword === "STARTCHAR") glyph = { encoding: -1, advance: 0, box: [0, 0, 0, 0] };
        else if (keyword === "ENCODING") glyph.encoding = numbers[0];
        else if (keyword === "DWIDTH") glyph.advance = numbers[0];
        else if (keyword === "BBX") glyph.box = numbers;
        else if (keyword === "BITMAP") bitmap = [];
        else if (keyword === "ENDCHAR") {
            glyph.rows = bitmap;
            bitmap = null;
            const usable = glyph.encoding >= 0 && glyph.encoding <= CODEPOINT_LIMIT && glyph.advance === font.width;
            if (usable) font.glyphs.push(glyph);
            glyph = null;
        }
    }
    font.height = font.ascent + font.descent;
    font.glyphs.sort((first, second) => first.encoding - second.encoding);
    return font;
}

function renderGlyph(font, glyph) {
    const pixels = Array.from({ length: font.height }, () => new Array(font.width).fill(false));
    const [boxWidth, boxHeight, boxX, boxY] = glyph.box;
    const rowBytes = Math.ceil(boxWidth / 8);
    for (let row = 0; row < boxHeight; row++) {
        const y = font.ascent - (boxY + boxHeight) + row;
        if (y < 0 || y >= font.height) continue;
        for (let column = 0; column < boxWidth; column++) {
            const x = boxX - font.offsetX + column;
            if (x < 0 || x >= font.width) continue;
            const bit = rowBytes * 8 - 1 - column;
            if ((glyph.rows[row] >> bit) & 1) pixels[y][x] = true;
        }
    }
    return pixels;
}

function embolden(pixels) {
    return pixels.map(row => row.map((ink, x) => ink || (x > 0 && row[x - 1])));
}

function writeAtlas(font, images, path) {
    const count = images.length;
    const atlasRows = Math.ceil(count / ATLAS_COLUMNS) * font.height;
    const stride = Math.ceil(ATLAS_COLUMNS * font.width / 16) * 2;
    const output = Buffer.alloc(HEADER_SIZE + count * 4 + atlasRows * stride, 0xff);
    output.write(MAGIC, 0, "latin1");
    output.writeUInt16LE(font.width, 4);
    output.writeUInt16LE(font.height, 6);
    output.writeUInt16LE(ATLAS_COLUMNS, 8);
    output.writeUInt16LE(stride, 10);
    output.writeUInt32LE(count, 12);
    font.glyphs.forEach((glyph, index) => output.writeUInt32LE(glyph.encoding, HEADER_SIZE + index * 4));
    const atlas = HEADER_SIZE + count * 4;
    images.forEach((pixels, index) => {
        const left = (index % ATLAS_COLUMNS) * font.width;
        const top = Math.floor(index / ATLAS_COLUMNS) * font.height;
        pixels.forEach((row, y) => row.forEach((ink, x) => {
            if (!ink) return;
            const bit = left + x;
            const offset = atlas + (top + y) * stride + (bit >> 3);
            output[offset] &= ~(0x80 >> (bit & 7));
        }));
    });
    fs.writeFileSync(path, output);
}

const [regularPath, outputPath, boldPath, boldOutputPath] = process.argv.slice(2);
const regular = readFont(regularPath);
const regularImages = regular.glyphs.map(glyph => renderGlyph(regular, glyph));
writeAtlas(regular, regularImages, outputPath);

if (boldOutputPath) {
    let boldImages = regularImages.map(embolden);
    if (boldPath !== "-") {
        const bold = readFont(boldPath);
        const byCodepoint = new Map(bold.glyphs.map(glyph => [glyph.encoding, glyph]));
        boldImages = regular.glyphs.map((glyph, index) => {
            const match = byCodepoint.get(glyph.encoding);
            return match ? renderGlyph(bold, match) : embolden(regularImages[index]);
        });
    }
    writeAtlas(regular, boldImages, boldOutputPath);
}
