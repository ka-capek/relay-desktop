#!/bin/bash
# Installs the Relay Native folder this script is in for the current user.
# It builds nothing, needs no root, and never touches profiles or repositories.
set -euo pipefail

usage() {
  cat <<'HELP'
Install Relay Native for the current user (Linux x86_64).

Usage: bash install.sh [--no-open]

Copies this folder to $XDG_DATA_HOME/relay-native (default
~/.local/share/relay-native) and adds an application menu entry. An existing
Relay Native there is kept as a dated "relay-native.previous-..." folder.

To uninstall, delete that folder and
~/.local/share/applications/dev.relay.gitclient.desktop. Accounts and settings
stay in ~/.config/relay-desktop.
HELP
}
fail() { printf '\nError: %s\n' "$*" >&2; exit 1; }
launch=1
while [ "$#" -gt 0 ]; do
  case "$1" in
    --help|-h) usage; exit 0 ;;
    --no-open) launch=0; shift ;;
    *) fail "Unknown option: $1 (use --help)." ;;
  esac
done

[ "$(uname -s)" = Linux ] || fail 'This installer requires Linux.'
[ "$(uname -m)" = x86_64 ] || fail 'This build supports 64-bit x86 (x86_64) only.'
[ "$(id -u)" -ne 0 ] || fail 'Run as your normal user, without sudo.'
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)
marker='"product": "dev.relay.native"'
[ -x "$here/Relay" ] && grep -qF "$marker" "$here/installation.json" 2>/dev/null ||
  fail 'Run install.sh from the extracted Relay Native folder.'

data="${XDG_DATA_HOME:-}"
case "$data" in /*) ;; *) data="$HOME/.local/share" ;; esac
destination="$data/relay-native"
# The path is written into a desktop entry, where these characters would need
# escaping rules that launchers implement differently.
case "$destination" in
  *[\"\`\$\\%]*|*$'\n'*) fail "The installation folder $destination contains characters a desktop entry cannot hold." ;;
esac
if pgrep -u "$(id -u)" -x Relay >/dev/null 2>&1; then fail 'Quit Relay before installing an update.'; fi

backup=''
if [ "$here" != "$destination" ]; then
  if [ -e "$destination" ] || [ -L "$destination" ]; then
    [ ! -L "$destination" ] && grep -qF "$marker" "$destination/installation.json" 2>/dev/null ||
      fail "$destination exists and is not a Relay Native installation. It was left unchanged."
  fi
  mkdir -p "$data"
  stage=$(mktemp -d "$data/.relay-native-install.XXXXXX")
  trap 'rm -rf -- "$stage"' EXIT
  cp -a "$here" "$stage/relay-native"
  if [ -e "$destination" ]; then
    backup="$data/relay-native.previous-$(date +%Y%m%d-%H%M%S)-$$"
    mv "$destination" "$backup"
  fi
  if ! mv "$stage/relay-native" "$destination"; then
    [ -z "$backup" ] || mv "$backup" "$destination"
    fail 'Could not install the new application; the previous one was restored.'
  fi
fi

applications="$data/applications"
mkdir -p "$applications"
entry="$applications/dev.relay.gitclient.desktop"
cat > "$entry.new" <<ENTRY
[Desktop Entry]
Type=Application
Name=Relay Native
GenericName=Git Client
Comment=Git desktop client with multiple accounts
Exec="$destination/Relay"
Icon=$destination/dev.relay.gitclient.png
Terminal=false
Categories=Development;RevisionControl;
StartupNotify=true
ENTRY
mv "$entry.new" "$entry"
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$applications" >/dev/null 2>&1 || true
fi

printf 'Installed: %s\n' "$destination"
[ -z "$backup" ] || printf 'Previous installation: %s\n' "$backup"
printf 'Open Relay Native from the application menu, then choose Connect GitHub account.\n'
if [ "$launch" -eq 1 ]; then
  nohup "$destination/Relay" >/dev/null 2>&1 &
fi
