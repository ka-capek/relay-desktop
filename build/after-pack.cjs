"use strict";

/**
 * Ad-hoc re-signs the packaged macOS app.
 *
 * Relay has no Developer ID, so electron-builder skips signing and the bundle
 * keeps the ad-hoc signature that shipped with the prebuilt Electron binary.
 * That signature no longer matches: electron-builder renames the executable,
 * adds resources, and rewrites Info.plist, which breaks the seal. The app then
 * reports `Sealed Resources=none` and fails `codesign --verify`.
 *
 * Intel Macs tolerate that. Apple Silicon does not: it requires every
 * executable to carry a valid signature, so it refuses to launch the app and
 * tells the user it is damaged and should be moved to the Trash.
 *
 * Re-signing ad-hoc rebuilds the seal against the real bundle contents and
 * restores dev.relay.gitclient as the identifier. It does not make the build
 * signed or notarized; a first-run Gatekeeper prompt is still expected. It only
 * turns a hard failure back into that ordinary prompt.
 */

const { execFileSync } = require("child_process");
const path = require("path");

exports.default = async function afterPack(context) {
  if (context.electronPlatformName !== "darwin") return;

  const appPath = path.join(context.appOutDir, `${context.packager.appInfo.productFilename}.app`);
  // --deep is discouraged for real distribution signing but is the supported
  // way to ad-hoc sign a bundle of nested Electron helpers and frameworks.
  execFileSync("codesign", ["--force", "--deep", "--sign", "-", appPath], { stdio: "inherit" });
  // Fail the build rather than ship an app that repeats the damaged report.
  execFileSync("codesign", ["--verify", "--deep", "--strict", appPath], { stdio: "inherit" });
  process.stdout.write(`  • ad-hoc signed and verified  ${path.basename(appPath)}\n`);
};
