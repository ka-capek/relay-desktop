#!/bin/bash
# Package an already deployed/smoke-tested native app. No compilation or login.
set -euo pipefail
[ "$#" -eq 3 ] || { echo 'Usage: build-preview-dmg.sh APP VERSION OUTPUT_DIR' >&2; exit 1; }
app=$1
version=$2
output=$3
[ "$(uname -s)" = Darwin ] && [ -f "$app/Contents/MacOS/Relay" ]
actual=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist")
[ "$actual" = "$version" ] || { echo 'Payload version mismatch' >&2; exit 1; }
/usr/bin/codesign --verify --deep --strict "$app"
mkdir -p "$output"
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
/usr/bin/ditto "$app" "$stage/Relay Native.app"
ln -s /Applications "$stage/Applications"
cat > "$stage/Install and update.txt" <<'HELP'
Quit Relay Native, then drag Relay Native.app to Applications.
For an update, choose Replace when Finder asks. No uninstall is needed.
If your existing native app is in ~/Applications or another folder, replace it
there instead of creating a second copy. Only replace Relay Native.app, not the
old Electron Relay.app. Your accounts, settings and repositories are unchanged.
Requires macOS 14+ on Apple Silicon, Git >=2.35 and GitHub CLI >=2.98.
This preview is ad-hoc signed, not notarized. Git and gh are external tools.
HELP
/usr/bin/hdiutil create -volname "Relay Native $version" -srcfolder "$stage" \
  -format UDZO -ov "$output/Relay-Native-$version-arm64.dmg"
/usr/bin/hdiutil verify "$output/Relay-Native-$version-arm64.dmg"
