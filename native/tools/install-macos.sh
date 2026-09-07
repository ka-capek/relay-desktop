#!/bin/bash
# Local source installer for Apple Silicon. Compatible with macOS Bash 3.2.
set -euo pipefail

# Restart in native mode before parsing so all arguments survive Rosetta.
if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" != arm64 ] &&
   [ "$(/usr/sbin/sysctl -in hw.optional.arm64 2>/dev/null || true)" = 1 ]; then
  exec /usr/bin/arch -arm64 /bin/bash "$0" "$@"
fi

usage() {
  cat <<'HELP'
Install Relay Native on an Apple Silicon Mac (macOS 14 or newer).

Usage: bash install-macos.sh [--no-open] [--source DIRECTORY]
                            [--qt-dir DIRECTORY] [--skip-deps]

By default, downloads codex/native-completion from ka-capek/relay-desktop,
installs build dependencies through Homebrew, builds with Qt 6.11.1, and
installs ~/Applications/Relay Native.app. Existing apps are backed up.

--no-open       Install without opening the application.
--source DIR    Build this checkout without changing its Git state.
--qt-dir DIR    Use an existing Qt 6.11.1 macOS SDK.
--skip-deps     Require preinstalled Homebrew, LLVM 20, Python 3.12, Git and gh.
--help         Show this help without changing anything.

Git/gh remain Homebrew dependencies; Qt is copied into the app. This is a
local build, not a notarized release. Several GB of free space are needed.
HELP
}
fail() { printf '\nError: %s\n' "$*" >&2; exit 1; }
info() { printf '\n==> %s\n' "$*"; }
source_dir=''
qt_dir=''
launch=1
skip_deps=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --no-open) launch=0; shift ;;
    --skip-deps) skip_deps=1; shift ;;
    --source|--qt-dir)
      [ "$#" -ge 2 ] && [ -n "$2" ] || fail "$1 needs a directory."
      if [ "$1" = --source ]; then source_dir="$2"; else qt_dir="$2"; fi
      shift 2 ;;
    *) fail "Unknown option: $1 (use --help)." ;;
  esac
done

[ "$(uname -s)" = Darwin ] || fail 'This installer requires macOS.'
if [ "$(uname -m)" != arm64 ]; then
  fail 'This installer supports Apple Silicon (M1 and newer), not Intel Macs.'
fi
[ "$EUID" -ne 0 ] || fail 'Run as your normal user, without sudo.'
mac_version=$(/usr/bin/sw_vers -productVersion)
[ "${mac_version%%.*}" -ge 14 ] || fail 'macOS 14 or newer is required by this installer.'
if ! /usr/bin/xcrun --find clang >/dev/null 2>&1; then
  /usr/bin/xcode-select --install || true
  fail 'Finish installing Xcode Command Line Tools, then run this script again.'
fi
if /usr/bin/pgrep -x Relay >/dev/null; then fail 'Quit Relay before installing an update.'; fi

cache="$HOME/Library/Caches/RelayNativeInstaller"
mkdir -p "$cache"
mkdir "$cache/install.lock" 2>/dev/null || fail "Another installation is running. If it was interrupted, remove $cache/install.lock and retry."
work=''
publish_stage=''
cleanup() {
  [ -z "$publish_stage" ] || rm -rf -- "$publish_stage"
  [ -z "$work" ] || rm -rf -- "$work"
  rmdir "$cache/install.lock" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'printf "\nInstallation failed at line %s. Your existing app and repositories were not deleted.\n" "$LINENO" >&2' ERR
work=$(mktemp -d "$cache/work.XXXXXX")

export PATH="/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"
brew=/opt/homebrew/bin/brew
if [ ! -x "$brew" ]; then
  [ "$skip_deps" -eq 0 ] || fail 'Homebrew is missing at /opt/homebrew.'
  info 'Installing Homebrew; its installer may ask for your macOS password.'
  /usr/bin/curl --fail --show-error --location --retry 3 --connect-timeout 15 --max-time 120 \
    https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh -o "$work/homebrew.sh"
  /bin/bash "$work/homebrew.sh"
fi
[ -x "$brew" ] || fail 'Complete Homebrew installation and run this script again.'
if [ "$skip_deps" -eq 0 ]; then
  info 'Preparing LLVM 20, Python 3.12, Git and GitHub CLI.'
  "$brew" install llvm@20 python@3.12 git gh
fi
compiler="$("$brew" --prefix llvm@20)/bin/clang++"
python="$("$brew" --prefix python@3.12)/bin/python3.12"
[ -x "$compiler" ] && [ -x "$python" ] || fail 'Install Homebrew llvm@20 and python@3.12 first.'
[ -x /opt/homebrew/bin/git ] && [ -x /opt/homebrew/bin/gh ] || fail 'Install Homebrew git and gh first.'

info 'Preparing isolated build tools.'
tools="$cache/tools"
"$python" -m venv "$tools"
"$tools/bin/python" -m pip install --disable-pip-version-check \
  cmake==3.31.6 ninja==1.13.2 aqtinstall==3.3.0
export PATH="$tools/bin:$PATH"
if [ -z "$qt_dir" ]; then
  qt_dir="$cache/Qt/6.11.1/macos"
  if [ ! -f "$qt_dir/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    info 'Downloading Qt 6.11.1.'
    "$tools/bin/python" -m aqt install-qt mac desktop 6.11.1 clang_64 --outputdir "$work/qt"
    [ -f "$work/qt/6.11.1/macos/lib/cmake/Qt6/Qt6Config.cmake" ] || fail 'Qt installation is incomplete.'
    mkdir -p "$cache/Qt"
    [ ! -e "$cache/Qt/6.11.1" ] || fail "The cached Qt SDK is incomplete. Move $cache/Qt/6.11.1 aside and retry."
    mv "$work/qt/6.11.1" "$cache/Qt/"
  fi
fi
qt_dir=$(cd "$qt_dir" && pwd -P)
[ -x "$qt_dir/bin/macdeployqt" ] || fail 'The Qt SDK must contain macdeployqt.'
if [ -z "$source_dir" ]; then
  info 'Downloading Relay source (codex/native-completion).'
  source_dir="$work/source"
  git clone --depth 1 --branch codex/native-completion \
    https://github.com/ka-capek/relay-desktop.git "$source_dir"
fi
source_dir=$(cd "$source_dir" && pwd -P)
[ -f "$source_dir/native/src/main.cpp" ] && [ -f "$source_dir/CMakeLists.txt" ] || fail 'The source directory is not a Relay checkout.'

info 'Building Relay for Apple Silicon.'
cmake -S "$source_dir" -B "$work/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DRELAY_BUILD_TESTS=OFF \
  -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_PREFIX_PATH="$qt_dir" -DRELAY_QT_MIN_VERSION=6.11.1
cmake --build "$work/build" --parallel 4
app="$work/stage/Relay.app"
mkdir -p "$work/stage"
/usr/bin/ditto "$work/build/native/Relay.app" "$app"

# Finder does not inherit a terminal's Homebrew PATH. Launch Services applies
# this environment before starting the app (not a global shell/profile change).
"$tools/bin/python" - "$app/Contents/Info.plist" <<'PY'
import plistlib
import sys
from pathlib import Path
path = Path(sys.argv[1])
with path.open('rb') as source:
    info = plistlib.load(source)
info.setdefault('LSEnvironment', {})['PATH'] = '/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin'
with path.open('wb') as target:
    plistlib.dump(info, target)
PY
licenses="$app/Contents/Resources/licenses"
mkdir -p "$licenses"
cp "$source_dir/LICENSE" "$licenses/Relay-MIT.txt"
for license in lgpl-3.0 gpl-3.0; do
  /usr/bin/curl --fail --show-error --location --retry 3 --connect-timeout 15 --max-time 120 \
    "https://www.gnu.org/licenses/$license.txt" -o "$licenses/$license.txt"
done
cat > "$licenses/Qt-source.txt" <<'NOTICE'
Qt 6.11.1 is dynamically linked, unmodified, under LGPLv3.
Corresponding source: https://download.qt.io/archive/qt/6.11/6.11.1/single/
Git and GitHub CLI are provided by the local Homebrew installation.
NOTICE
info 'Deploying Qt and checking the application.'
"$qt_dir/bin/macdeployqt" "$app" -always-overwrite
/usr/bin/codesign --force --deep --sign - "$app"
/usr/bin/codesign --verify --deep --strict "$app"
"$tools/bin/python" - "$app/Contents/MacOS/Relay" <<'PY'
import os
import subprocess
import sys
env = os.environ.copy()
for key in ('QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH'):
    env.pop(key, None)
env['PATH'] = '/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin'
subprocess.run([sys.argv[1], '--smoke-test'], env=env, check=True, timeout=30)
PY

applications="$HOME/Applications"
destination="$applications/Relay Native.app"
mkdir -p "$applications"
publish_stage=$(mktemp -d "$applications/.relay-install.XXXXXX")
/usr/bin/ditto "$app" "$publish_stage/Relay.app"
if /usr/bin/pgrep -x Relay >/dev/null; then fail 'Quit Relay, then run the installer again.'; fi
backup=''
if [ -e "$destination" ] || [ -L "$destination" ]; then
  backup="$applications/Relay Native previous $(date +%Y%m%d-%H%M%S)-$$.app"
  mv "$destination" "$backup"
fi
if ! mv "$publish_stage/Relay.app" "$destination"; then
  [ -z "$backup" ] || mv "$backup" "$destination"
  fail 'Could not install the new application; the previous app was restored.'
fi
info "Installed: $destination"
[ -z "$backup" ] || printf 'Previous app: %s\n' "$backup"
printf 'Open Relay Native from Finder, then choose Connect GitHub account.\n'
if [ "$launch" -eq 1 ]; then /usr/bin/open "$destination"; fi
