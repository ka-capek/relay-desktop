#!/bin/bash
# Safety checks for native/packaging/preview/linux-install.sh with a fake
# payload and a disposable XDG_DATA_HOME. Builds and starts nothing.
set -euo pipefail
script=$(cd "$(dirname "${BASH_SOURCE[0]}")/../packaging/preview" && pwd -P)/linux-install.sh
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT
fail() { printf 'FAIL: %s\n' "$*" >&2; exit 1; }

# The installer's logic is plain POSIX; a shim lets other hosts run this test.
mkdir "$root/bin"
cat > "$root/bin/uname" <<'SHIM'
#!/bin/sh
case "$1" in -s) echo Linux ;; -m) echo x86_64 ;; *) echo Linux ;; esac
SHIM
printf '#!/bin/sh\nexit 1\n' > "$root/bin/pgrep"
chmod +x "$root/bin/uname" "$root/bin/pgrep"
export PATH="$root/bin:$PATH"
export HOME="$root/home"
export XDG_DATA_HOME="$root/data"
mkdir -p "$HOME"

payload() {  # payload DIRECTORY VERSION
  mkdir -p "$1"
  printf '#!/bin/sh\necho %s\n' "$2" > "$1/Relay"
  chmod +x "$1/Relay"
  printf '{\n  "product": "dev.relay.native",\n  "version": "%s"\n}\n' "$2" > "$1/installation.json"
  : > "$1/dev.relay.gitclient.png"
  cp "$script" "$1/install.sh"
}
destination="$XDG_DATA_HOME/relay-native"
entry="$XDG_DATA_HOME/applications/dev.relay.gitclient.desktop"

payload "$root/first/relay-native" 1.0.0
bash "$root/first/relay-native/install.sh" --no-open >/dev/null
[ "$("$destination/Relay")" = 1.0.0 ] || fail 'fresh installation'
grep -qxF "Exec=\"$destination/Relay\"" "$entry" || fail 'desktop entry Exec'
grep -qxF "Icon=$destination/dev.relay.gitclient.png" "$entry" || fail 'desktop entry Icon'
[ -d "$root/first/relay-native" ] || fail 'the extracted folder must stay'

# An upgrade keeps the previous installation and user files beside it.
: > "$XDG_DATA_HOME/unrelated.txt"
payload "$root/second/relay-native" 2.0.0
bash "$root/second/relay-native/install.sh" --no-open >/dev/null
[ "$("$destination/Relay")" = 2.0.0 ] || fail 'upgrade'
previous=$(find "$XDG_DATA_HOME" -maxdepth 1 -name 'relay-native.previous-*' | head -n 1)
[ -n "$previous" ] && [ "$("$previous/Relay")" = 1.0.0 ] || fail 'previous installation kept'
[ -f "$XDG_DATA_HOME/unrelated.txt" ] || fail 'unrelated files kept'
[ -z "$(find "$XDG_DATA_HOME" -maxdepth 1 -name '.relay-native-install.*')" ] || fail 'staging folder removed'

# Running the installed copy again only refreshes the menu entry.
rm "$entry"
bash "$destination/install.sh" --no-open >/dev/null
[ -f "$entry" ] && [ "$("$destination/Relay")" = 2.0.0 ] || fail 'reinstall in place'

# A folder that is not Relay's is refused and left alone.
export XDG_DATA_HOME="$root/other"
mkdir -p "$XDG_DATA_HOME/relay-native"
echo keep > "$XDG_DATA_HOME/relay-native/notes.txt"
if bash "$root/second/relay-native/install.sh" --no-open >/dev/null 2>&1; then fail 'unrelated folder accepted'; fi
[ "$(cat "$XDG_DATA_HOME/relay-native/notes.txt")" = keep ] || fail 'unrelated folder changed'
[ ! -e "$XDG_DATA_HOME/applications" ] || fail 'menu entry written after a refusal'

# So is a symbolic link, a running Relay, and a folder that is not a payload.
export XDG_DATA_HOME="$root/linked"
mkdir -p "$XDG_DATA_HOME"
ln -s "$destination" "$XDG_DATA_HOME/relay-native"
if bash "$root/second/relay-native/install.sh" --no-open >/dev/null 2>&1; then fail 'symbolic link accepted'; fi
export XDG_DATA_HOME="$root/running"
printf '#!/bin/sh\nexit 0\n' > "$root/bin/pgrep"
if bash "$root/second/relay-native/install.sh" --no-open >/dev/null 2>&1; then fail 'installed over a running Relay'; fi
[ ! -e "$XDG_DATA_HOME/relay-native" ] || fail 'running Relay was replaced'
printf '#!/bin/sh\nexit 1\n' > "$root/bin/pgrep"
mkdir "$root/empty"
cp "$script" "$root/empty/install.sh"
if bash "$root/empty/install.sh" --no-open >/dev/null 2>&1; then fail 'non-payload folder accepted'; fi
echo 'Linux installer safety checks passed.'
