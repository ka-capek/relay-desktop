#!/bin/bash
# Local source installer for Linux x86_64. Never runs sudo.
set -euo pipefail

usage() {
  cat <<'HELP'
Build and install Relay Native on Linux x86_64 (glibc; Ubuntu 24.04, Debian 13,
Fedora 40 or newer are the tested baseline).

Usage: bash install-linux.sh [--no-open] [--source DIRECTORY] [--qt-dir DIRECTORY]
                            [--runtimes DIRECTORY] [--compiler PROGRAM]
                            [--package DIRECTORY] [--no-install]

By default, downloads main from ka-capek/relay-desktop, Qt 6.11.1 and the
pinned GitHub CLI, builds with clang++ 20 or newer, and installs to
~/.local/share/relay-native with an application menu entry. An existing
installation is kept as a dated backup.

--no-open        Install without opening the application.
--source DIR     Build this checkout without changing its Git state.
--qt-dir DIR     Use an existing Qt 6.11.1 gcc_64 SDK.
--runtimes DIR   Use the GitHub CLI from native/tools/fetch_runtimes.py output.
--compiler PROG  Use this clang++ instead of looking for clang++-20/clang++.
--package DIR    Also write Relay-Native-<version>-linux-x64.tar.gz there.
--no-install     Build, check and package only.
--help           Show this help without changing anything.

The script never uses sudo: missing system packages are listed with the
command that installs them. Git stays a system dependency; Qt and the GitHub
CLI are copied into the application folder. This is a local, unsigned build.
HELP
}
fail() { printf '\nError: %s\n' "$*" >&2; exit 1; }
info() { printf '\n==> %s\n' "$*"; }
source_dir=''
qt_dir=''
runtimes=''
compiler=''
package_dir=''
launch=1
install=1
while [ "$#" -gt 0 ]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --no-open) launch=0; shift ;;
    --no-install) install=0; shift ;;
    --source|--qt-dir|--runtimes|--compiler|--package)
      [ "$#" -ge 2 ] && [ -n "$2" ] || fail "$1 needs a value."
      case "$1" in
        --source) source_dir="$2" ;;
        --qt-dir) qt_dir="$2" ;;
        --runtimes) runtimes="$2" ;;
        --compiler) compiler="$2" ;;
        *) package_dir="$2" ;;
      esac
      shift 2 ;;
    *) fail "Unknown option: $1 (use --help)." ;;
  esac
done

[ "$(uname -s)" = Linux ] || fail 'This installer requires Linux.'
[ "$(uname -m)" = x86_64 ] || fail 'This installer supports 64-bit x86 (x86_64) only.'
[ "$(id -u)" -ne 0 ] || fail 'Run as your normal user, without sudo.'
if [ -n "$runtimes" ]; then
  runtimes=$(cd "$runtimes" 2>/dev/null && pwd -P) || fail '--runtimes must be an existing directory.'
fi

# System packages are the user's to install; say exactly what is missing.
missing=''
need() { missing="$missing $1"; }
if [ -z "$compiler" ]; then
  for candidate in clang++-20 clang++; do
    if command -v "$candidate" >/dev/null 2>&1 &&
       [ "$("$candidate" -dumpversion | cut -d. -f1)" -ge 20 ]; then
      compiler=$(command -v "$candidate")
      break
    fi
  done
  [ -n "$compiler" ] || need 'clang++ 20 or newer'
else
  command -v "$compiler" >/dev/null 2>&1 || fail "--compiler $compiler was not found."
  compiler=$(command -v "$compiler")
fi
command -v git >/dev/null 2>&1 || need git
command -v python3 >/dev/null 2>&1 && python3 -c 'import venv, ensurepip' >/dev/null 2>&1 || need 'python3 with venv'
command -v pkg-config >/dev/null 2>&1 || need pkg-config
if command -v pkg-config >/dev/null 2>&1; then
  pkg-config --exists libsecret-1 || need 'libsecret development files'
  pkg-config --exists xkbcommon || need 'libxkbcommon development files'
fi
[ -e /usr/include/GL/gl.h ] || need 'OpenGL development headers'
if [ -n "$missing" ]; then
  printf '\nMissing:%s\n\nInstall the build dependencies, then run this script again:\n' "$missing" >&2
  cat >&2 <<'HELP'
  Debian/Ubuntu: sudo apt install clang-20 g++ git python3-venv pkg-config libsecret-1-dev libgl-dev libxkbcommon-dev libxcb-cursor0
  Fedora:        sudo dnf install clang gcc-c++ git python3 pkgconf-pkg-config libsecret-devel mesa-libGL-devel libxkbcommon-devel xcb-util-cursor
  Arch:          sudo pacman -S clang gcc git python pkgconf libsecret libglvnd libxkbcommon xcb-util-cursor
HELP
  exit 1
fi

cache="${XDG_CACHE_HOME:-}"
case "$cache" in /*) ;; *) cache="$HOME/.cache" ;; esac
cache="$cache/relay-native-installer"
mkdir -p "$cache"
mkdir "$cache/install.lock" 2>/dev/null || fail "Another installation is running. If it was interrupted, remove $cache/install.lock and retry."
work=''
cleanup() {
  [ -z "$work" ] || rm -rf -- "$work"
  rmdir "$cache/install.lock" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'printf "\nInstallation failed at line %s. Your existing app and repositories were not deleted.\n" "$LINENO" >&2' ERR
work=$(mktemp -d "$cache/work.XXXXXX")

info 'Preparing isolated build tools.'
tools="$cache/tools"
python3 -m venv "$tools"
"$tools/bin/python" -m pip install --disable-pip-version-check \
  cmake==3.31.6 ninja==1.13.2 aqtinstall==3.3.0
export PATH="$tools/bin:$PATH"
if [ -z "$qt_dir" ]; then
  qt_dir="$cache/Qt/6.11.1/gcc_64"
  if [ ! -f "$qt_dir/lib/cmake/Qt6/Qt6Config.cmake" ]; then
    info 'Downloading Qt 6.11.1.'
    "$tools/bin/python" -m aqt install-qt linux desktop 6.11.1 linux_gcc_64 --outputdir "$work/qt"
    [ -f "$work/qt/6.11.1/gcc_64/lib/cmake/Qt6/Qt6Config.cmake" ] || fail 'Qt installation is incomplete.'
    mkdir -p "$cache/Qt"
    [ ! -e "$cache/Qt/6.11.1" ] || fail "The cached Qt SDK is incomplete. Move $cache/Qt/6.11.1 aside and retry."
    mv "$work/qt/6.11.1" "$cache/Qt/"
  fi
fi
qt_dir=$(cd "$qt_dir" && pwd -P)
[ -f "$qt_dir/lib/libQt6Core.so.6" ] && [ -d "$qt_dir/plugins/platforms" ] || fail 'The Qt SDK must be a Qt 6.11.1 gcc_64 installation.'
if [ -z "$source_dir" ]; then
  info 'Downloading Relay source (main).'
  source_dir="$work/source"
  git clone --depth 1 --branch main \
    https://github.com/ka-capek/relay-desktop.git "$source_dir"
fi
source_dir=$(cd "$source_dir" && pwd -P)
[ -f "$source_dir/native/src/main.cpp" ] && [ -f "$source_dir/CMakeLists.txt" ] || fail 'The source directory is not a Relay checkout.'
version=$(sed -n 's/^  VERSION \(.*\)$/\1/p' "$source_dir/CMakeLists.txt")
[ -n "$version" ] || fail 'Could not read the Relay version from CMakeLists.txt.'
if [ -z "$runtimes" ]; then
  # Distribution packages of gh are usually older than Relay requires.
  info 'Downloading the pinned GitHub CLI.'
  runtimes="$work/runtimes"
  "$tools/bin/python" "$source_dir/native/tools/fetch_runtimes.py" --platform linux-x64 \
    --output "$runtimes" --cache "$cache/downloads"
fi
[ -x "$runtimes/gh/linux-x64/gh" ] || fail 'The runtimes directory has no gh/linux-x64/gh; run native/tools/fetch_runtimes.py first.'

info 'Building Relay for Linux x86_64.'
app="$work/stage/relay-native"
# The single-quoted $ORIGIN is for the dynamic loader, not the shell.
# shellcheck disable=SC2016
cmake -S "$source_dir" -B "$work/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DRELAY_BUILD_TESTS=OFF \
  -DCMAKE_CXX_COMPILER="$compiler" -DCMAKE_PREFIX_PATH="$qt_dir" \
  -DRELAY_QT_MIN_VERSION=6.11.1 '-DCMAKE_INSTALL_RPATH=$ORIGIN/lib'
cmake --build "$work/build" --parallel 4
cmake --install "$work/build" --prefix "$app"
[ -x "$app/Relay" ] || fail 'The build did not produce the Relay executable.'

info 'Deploying Qt and checking the application.'
mkdir -p "$app/lib" "$app/plugins"
for group in platforms platformthemes platforminputcontexts imageformats iconengines xcbglintegrations tls \
             wayland-shell-integration wayland-decoration-client wayland-graphics-integration-client; do
  [ ! -d "$qt_dir/plugins/$group" ] || cp -a "$qt_dir/plugins/$group" "$app/plugins/"
done
find "$app/plugins" -name '*.debug' -delete
# Official Qt libraries look for each other beside themselves and plugins
# look in ../../lib, so the Qt libraries they resolve to go into lib/ as is.
qt_libraries() {
  LD_LIBRARY_PATH="$qt_dir/lib" ldd "$@" 2>/dev/null |
    awk -v root="$qt_dir/lib/" '$2 == "=>" && index($3, root) == 1 { print $3 }' | sort -u
}
{
  qt_libraries "$app/Relay"
  find "$app/plugins" -name '*.so' -print0 | while IFS= read -r -d '' plugin; do qt_libraries "$plugin"; done
} | sort -u > "$work/qt-libraries.txt"
[ -s "$work/qt-libraries.txt" ] || fail 'No Qt libraries were found to deploy.'
while IFS= read -r library; do
  cp -L "$library" "$app/lib/"
done < "$work/qt-libraries.txt"
printf '[Paths]\nPrefix = .\n' > "$app/qt.conf"

mkdir -p "$app/resources/gh" "$app/licenses"
cp "$runtimes/gh/linux-x64/gh" "$app/resources/gh/gh"
cp "$runtimes/licenses/linux-x64/"* "$app/licenses/"
cp "$source_dir/LICENSE" "$app/licenses/Relay-MIT.txt"
cp "$source_dir/native/licenses/LGPL-3.0.txt" "$app/licenses/lgpl-3.0.txt"
cp "$source_dir/native/licenses/GPL-3.0.txt" "$app/licenses/gpl-3.0.txt"
cat > "$app/licenses/Qt-source.txt" <<'NOTICE'
Qt 6.11.1 is dynamically linked, unmodified, under LGPLv3.
Corresponding source: https://download.qt.io/archive/qt/6.11/6.11.1/single/
Git is provided by the Linux distribution.
NOTICE
cp "$source_dir/build/icon.png" "$app/dev.relay.gitclient.png"
cp "$source_dir/native/packaging/preview/linux-install.sh" "$app/install.sh"
chmod 755 "$app/install.sh" "$app/Relay" "$app/resources/gh/gh"
printf '{\n  "product": "dev.relay.native",\n  "platform": "linux-x64",\n  "version": "%s"\n}\n' "$version" > "$app/installation.json"

# The staged application must stand on its own: no library from the SDK or
# the build tree, and none missing.
resolved=$(env -u LD_LIBRARY_PATH ldd "$app/Relay")
if printf '%s\n' "$resolved" | grep -q 'not found'; then
  printf '%s\n' "$resolved" >&2
  fail 'The staged application has unresolved libraries.'
fi
if printf '%s\n' "$resolved" | grep -qF -e "$qt_dir" -e "$work/build"; then
  printf '%s\n' "$resolved" >&2
  fail 'The staged application still loads libraries from the Qt SDK or the build tree.'
fi
if find "$app/plugins" -name '*.so' -exec env -u LD_LIBRARY_PATH ldd {} + 2>/dev/null | grep -E 'libQt6[^ ]* => not found'; then
  fail 'A deployed Qt plugin cannot find its Qt libraries.'
fi
"$app/resources/gh/gh" --version >/dev/null || fail 'The bundled GitHub CLI does not run.'
smoke_platform=()
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then smoke_platform=(QT_QPA_PLATFORM=offscreen); fi
env -u QT_PLUGIN_PATH -u LD_LIBRARY_PATH -u QT_QPA_PLATFORM "${smoke_platform[@]}" \
  timeout 30 "$app/Relay" --smoke-test || fail 'The staged application did not pass its startup check.'

if [ -n "$package_dir" ]; then
  mkdir -p "$package_dir"
  package_dir=$(cd "$package_dir" && pwd -P)
  archive="$package_dir/Relay-Native-$version-linux-x64.tar.gz"
  tar -C "$work/stage" --owner=0 --group=0 --numeric-owner -czf "$archive" relay-native
  info "Packaged: $archive"
fi
if [ "$install" -eq 1 ]; then
  info 'Installing for the current user.'
  if [ "$launch" -eq 1 ]; then bash "$app/install.sh"; else bash "$app/install.sh" --no-open; fi
fi
