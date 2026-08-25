"use strict";

/**
 * Regenerates Relay's packaged application icons.
 *
 *   npm run icons:build
 *
 * electron-builder can derive an .icns and .ico from a single PNG, but the
 * .icns it produced for 0.4.0 put a 512px image in the 128@2x slot and a
 * 1024px image in the 256@2x slot, so macOS rescaled the icon at every Retina
 * Dock and Finder size. This script writes both containers itself with the
 * correct type-to-size mapping.
 *
 * Chromium does the rasterizing because it is the one SVG renderer this
 * repository already depends on; ImageMagick's SVG support drops stroked
 * paths, which is most of the mark. Skia does the downsampling.
 */

const fs = require("fs");
const path = require("path");
const { app, BrowserWindow, nativeImage } = require("electron");

const buildDirectory = __dirname;
const MASTER_SIZE = 1024;

// macOS switches to the small-size master below 64px, where the 1024px
// artwork's strokes fall under two device pixels and disappear.
const SMALL_SIZE_LIMIT = 48;

// type -> pixel size. Apple's icns types are fixed sizes, not scale factors:
// ic11 is 16@2x (32px), ic13 is 128@2x (256px), ic14 is 256@2x (512px).
const ICNS_ENTRIES = [
  ["icp4", 16],
  ["icp5", 32],
  ["icp6", 64],
  ["ic07", 128],
  ["ic08", 256],
  ["ic09", 512],
  ["ic10", 1024],
  ["ic11", 32],
  ["ic12", 64],
  ["ic13", 256],
  ["ic14", 512],
];

const ICO_SIZES = [16, 24, 32, 48, 64, 128, 256];

// A capture taken before compositing settles is transparent or half drawn, and
// silently shipping that would replace the icon with an empty tile.
function assertPainted(image, svgFileName) {
  const { width, height } = image.getSize();
  const bitmap = image.toBitmap();
  const centre = ((Math.floor(height / 2) * width) + Math.floor(width / 2)) * 4;
  if (bitmap[centre + 3] < 250) throw new Error(`${svgFileName} captured before it finished painting.`);
}

function masterFor(size) {
  return size <= SMALL_SIZE_LIMIT ? "icon-small.svg" : "icon.svg";
}

async function rasterize(window, svgFileName) {
  const svg = fs.readFileSync(path.join(buildDirectory, svgFileName), "utf8");
  const page = `<!doctype html><meta charset="utf-8"><style>
    html,body{margin:0;padding:0;background:transparent;}
    svg{display:block;width:${MASTER_SIZE}px;height:${MASTER_SIZE}px;}
  </style>${svg}`;

  await window.loadURL(`data:text/html;charset=utf-8,${encodeURIComponent(page)}`);

  // loadURL resolves before the first paint, and capturing early produces a
  // partly composited icon whose bytes differ from run to run.
  await window.webContents.executeJavaScript(
    "new Promise((done) => requestAnimationFrame(() => requestAnimationFrame(done)))",
  );

  const captured = await window.webContents.capturePage();
  if (captured.isEmpty()) throw new Error(`${svgFileName} rendered as an empty image.`);
  assertPainted(captured, svgFileName);

  // capturePage returns device pixels, so this is 2048px on a Retina Mac.
  const { width } = captured.getSize();
  return width === MASTER_SIZE ? captured : captured.resize({ width: MASTER_SIZE, height: MASTER_SIZE, quality: "best" });
}

function pngAt(masters, size) {
  const master = masters[masterFor(size)];
  const scaled = size === MASTER_SIZE ? master : master.resize({ width: size, height: size, quality: "best" });
  const png = scaled.toPNG();
  if (!png.length) throw new Error(`Failed to encode a ${size}px PNG.`);
  return png;
}

function buildIcns(pngBySize) {
  const chunks = [];
  for (const [type, size] of ICNS_ENTRIES) {
    const payload = pngBySize.get(size);
    const header = Buffer.alloc(8);
    header.write(type, 0, 4, "ascii");
    header.writeUInt32BE(payload.length + 8, 4);
    chunks.push(header, payload);
  }
  const body = Buffer.concat(chunks);
  const header = Buffer.alloc(8);
  header.write("icns", 0, 4, "ascii");
  header.writeUInt32BE(body.length + 8, 4);
  return Buffer.concat([header, body]);
}

function buildIco(pngBySize) {
  const header = Buffer.alloc(6);
  header.writeUInt16LE(0, 0);
  header.writeUInt16LE(1, 2); // 1 = icon
  header.writeUInt16LE(ICO_SIZES.length, 4);

  const directory = Buffer.alloc(16 * ICO_SIZES.length);
  const payloads = [];
  let offset = header.length + directory.length;

  ICO_SIZES.forEach((size, index) => {
    const payload = pngBySize.get(size);
    const entry = index * 16;
    directory.writeUInt8(size >= 256 ? 0 : size, entry + 0); // 0 means 256
    directory.writeUInt8(size >= 256 ? 0 : size, entry + 1);
    directory.writeUInt8(0, entry + 2); // truecolour, no palette
    directory.writeUInt8(0, entry + 3); // reserved
    directory.writeUInt16LE(1, entry + 4); // colour planes
    directory.writeUInt16LE(32, entry + 6); // bits per pixel
    directory.writeUInt32LE(payload.length, entry + 8);
    directory.writeUInt32LE(offset, entry + 12);
    payloads.push(payload);
    offset += payload.length;
  });

  return Buffer.concat([header, directory, ...payloads]);
}

async function main() {
  const window = new BrowserWindow({
    width: MASTER_SIZE,
    height: MASTER_SIZE,
    show: false,
    frame: false,
    transparent: true,
    backgroundColor: "#00000000",
    useContentSize: true,
    webPreferences: { offscreen: false, sandbox: true },
  });

  const masters = {};
  for (const svgFileName of new Set(["icon.svg", "icon-small.svg"])) {
    masters[svgFileName] = await rasterize(window, svgFileName);
  }

  const pngBySize = new Map();
  for (const size of new Set([...ICNS_ENTRIES.map(([, value]) => value), ...ICO_SIZES, MASTER_SIZE])) {
    pngBySize.set(size, pngAt(masters, size));
  }

  const written = [
    ["icon.icns", buildIcns(pngBySize)],
    ["icon.ico", buildIco(pngBySize)],
    ["icon.png", pngBySize.get(MASTER_SIZE)],
  ];
  for (const [name, contents] of written) {
    fs.writeFileSync(path.join(buildDirectory, name), contents);
    process.stdout.write(`${name.padEnd(10)} ${String(contents.length).padStart(9)} bytes\n`);
  }

  window.destroy();
}

// Software rasterizing keeps the generated bytes identical across machines and
// across runs on the same machine.
app.disableHardwareAcceleration();

app.whenReady()
  .then(main)
  .then(() => app.exit(0))
  .catch((error) => {
    process.stderr.write(`${error.stack || error.message}\n`);
    app.exit(1);
  });
