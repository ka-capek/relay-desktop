# Relay

Relay is a small GitHub Desktop-style client focused on easy multi-account
switching. Relay 0.5.0 is the shipping Electron client for Apple silicon Macs
and 64-bit Windows. A C++26/Qt 6 native successor now lives alongside it under
`native/`; the Electron client remains the parity specification until the
native release passes its cross-platform stop/go gate.

## Download

Native preview: [**Relay 0.5.2 — Native installers and upgrades**](https://github.com/ka-capek/relay-desktop/releases/tag/v0.5.2)
is a prerelease of the C++26/Qt client. Its Windows setup and macOS DMG require
external Git >=2.35.0 and GitHub CLI >=2.98.0. They are unsigned (macOS uses
ad-hoc signing), are not notarized, and do not bundle Git/gh. Real multi-account OAuth,
private clone/fetch/push and publication still need installed-platform
verification. Relay 0.5.0 remains the stable Electron release.

Download the ready-to-use installers from the
[latest GitHub release](https://github.com/ka-capek/relay-desktop/releases/latest):

- Windows 64-bit: `Relay-Setup-0.5.0-x64.exe`
- macOS Apple Silicon: `Relay-0.5.0-arm64.dmg`

The builds are currently unsigned. On macOS, right-click Relay in Applications
and choose **Open** the first time; Windows SmartScreen may also ask for
confirmation. The macOS app is ad-hoc signed during packaging, which is what
stops Apple Silicon reporting it as damaged.

## Native installers and upgrades (0.5.2 onward)

Native prereleases provide a Windows **Relay-Native-Setup-<version>-x64.exe**
and an Apple Silicon **Relay-Native-<version>-arm64.dmg**. These are unsigned
preview distributions with external Git and GitHub CLI, as described above.

On Windows, the wizard lets you choose the install folder and optionally add a
desktop shortcut. Subsequent installers remember that folder and update the
native client in place: no manual uninstall is needed. Quit Relay Native before
updating. The default is `%LOCALAPPDATA%/Programs/Relay Native`; installation
requires no administrator privileges. The 0.5.1 native ZIP can be adopted by
selecting its extracted folder once. Unrelated and Electron folders are refused.
Use Setup for later updates of an installation managed by Setup.

On macOS, quit Relay Native, open the DMG, drag **Relay Native.app** into
Applications and confirm **Replace**. If the old native app is in
`~/Applications` or another folder, replace it there. Do not replace the old
Electron `Relay.app`. No uninstall is needed. Account settings, CLI credentials
and repositories live outside the application and are preserved on both systems.
There is no background update download: run each new installer yourself.

## Prerequisites

- Node.js `>=22.13.0` (development only)
- Git and the official GitHub CLI when running from source

## Quick Start

```bash
npm install
npm run desktop:open
```

## Install the native client on Apple Silicon

On macOS 14 or newer, download and run the installer:

```bash
curl -fL https://raw.githubusercontent.com/ka-capek/relay-desktop/codex/native-completion/native/tools/install-macos.sh -o /tmp/relay-install-macos.sh
bash /tmp/relay-install-macos.sh
```

It prepares Homebrew dependencies, downloads the current native branch, builds
with LLVM 20 and Qt 6.11.1, deploys Qt, verifies a local signature and startup,
and installs `~/Applications/Relay Native.app`. It opens the app on completion;
use `--no-open` to skip that. Homebrew may request your macOS password. If Xcode
Command Line Tools are missing, finish their installation and rerun the script.

An existing native app is saved beside the new one as a dated backup. Your
repositories, checkout and account settings are not changed by the installer.
Build tools and Qt are cached under `~/Library/Caches/RelayNativeInstaller`.
Git and GitHub CLI remain Homebrew dependencies; keep them installed. The app's
Launch Services environment includes Homebrew, so it also works from Finder.
This is a local source build, not a notarized distribution.

To install an existing checkout, run `bash native/tools/install-macos.sh --source "$PWD"`; `--qt-dir /path/to/Qt/6.11.1/macos` reuses an existing SDK.
`--skip-deps` is available when the required Homebrew tools are already present.

## Install the native client on Windows x64

From an existing checkout, run in x64 PowerShell:

```powershell
./native/tools/install-windows.ps1 -NoOpen
```

Prerequisites: Visual Studio C++ Build Tools with the Desktop development with
C++ workload and x64 Windows SDK, LLVM 20, Python 3.12 on PATH, Git, GitHub CLI,
and 7-Zip. The script selects the Visual Studio developer environment itself.
Use `-QtDir C:/Qt/6.11.1/msvc2022_64` to reuse an SDK, or it downloads the pinned
Qt version into a user cache. Build tools are isolated in that cache.

The installer builds Release, deploys Qt and the MSVC runtime, and launches a
temporary-profile smoke test with the Qt SDK removed from PATH. Only after that
passes does it install `%LOCALAPPDATA%/Programs/Relay Native/Relay.exe` and add a
**Relay Native** Start menu shortcut. Quit Relay before updating. A previous
installation is retained beside the new one; a failed replacement restores it.
Repository files and account settings are not modified by installation.

Omit `-NoOpen` to launch after installation. `-Source` selects another checkout;
`-InstallDirectory` selects a separate application directory. Keep Git and gh
installed: they are external dependencies. This is an unsigned local source
build, not the published Electron installer. The new Windows installer still
needs its own native CI run; see the [completion record](docs/native-completion-2026-09-14.md).

## Native first-run setup

Relay checks Git and GitHub CLI on startup without signing in. Missing or old
tools produce a persistent banner with installation guidance. Choose **Check
again** after installing, or **Help → Check Git and GitHub CLI** at any time.
The supported baselines are Git 2.35.0 and GitHub CLI 2.98.0 (the previous bundled
CLI version). Local repositories remain usable without a GitHub account.

Windows also checks the standard Git and GitHub CLI installation directories
when an already-running app has an old PATH. A custom installation directory
must already be on the app's PATH; otherwise restart after updating PATH.

## Native sync, diff sizing and SSH selection

Use the arrow beside the toolbar sync button to choose **Fetch origin**,
**Pull origin** or **Push origin** directly. Pull fetches first and then
fast-forwards the tracked origin branch; divergent branches still require an
explicit merge/rebase. The main button continues to suggest an action from the
last known repository state.

Drag the dividers to resize changes/history panels. In History, the horizontal
divider above the diff also changes the space shared by commit details/files
and the diff.

For an SSH repository (including GitHub), open the top-right account/identity menu and
select a saved SSH identity for that host. No GitHub sign-in is needed. Choose
**Use SSH agent and configuration** to clear the override. Commit author details
remain independent of the SSH key and can be set through Settings/local Git.
For GitHub, use a remote such as git@github.com:owner/repo.git and a profile
with host github.com and user git. Clone, fetch, pull and push use that key;
HTTPS remotes continue to use the selected GitHub OAuth account.

## Native themes and branch graph

Open Settings (`⌘,` on macOS) → **Appearance** to choose **Light**, **Dark**,
**Catppuccin Latte**, or **Catppuccin Mocha**. Save applies the theme immediately
and remembers it across restarts. Menus, controls, lists, diffs and the graph
share the same palette.

Use **Export…** for an editable JSON palette, then **Import…** to load your own
or a shared theme. You can also enter only the colors you want to override:

```json
{
  "accent": "#cba6f7",
  "accentHover": "#b4befe",
  "selection": "#45405c",
  "onAccent": "#1e1e2e",
  "branches": ["#cba6f7", "#89b4fa", "#a6e3a1", "#fab387"]
}
```

Overrides stay active when changing the base theme; **Reset overrides** restores
its defaults. Importing copies colors into Relay's settings; it does not retain
a dependency on the source file or execute theme code. Colors use `#RRGGBB`;
`branches` accepts 2–32 colors. Export includes every supported color role.
The [bundled palettes](native/resources/themes) are additional examples.
Choose contrasting text/background and accent/onAccent pairs for custom themes.

In History, select **Graph** beside the search field to show all branches.
Parallel lines retain their colors when lanes move or another branch ends,
including across loaded pages. Commit dots and ref labels use the same colors;
merge dots are hollow. Colors are reused after a line ends and repeat if more
lines are active than the palette has colors. Search temporarily hides graph
connections so filtered-out commits cannot imply a false connection. Return to
**List** for the ordinary current-branch history.

Catppuccin presets adapt the [Catppuccin palette](https://github.com/catppuccin/catppuccin),
with darker Latte status/graph colors for readability. Chevron icons come from
[Lucide](https://github.com/lucide-icons/lucide). Their licenses are included in
the application resources.

## Native C++26 development

The native client requires CMake 3.30+, Ninja, and the pinned Qt 6.11.1 with
Core, Gui, Widgets, Network, Svg, Concurrent, and Test. Qt is dynamically
linked. The presets use upstream LLVM 20 `clang++` for C++26 (the default
AppleClang/MSVC compiler modes are insufficient for this CMake configuration).
On Apple Silicon, install `llvm@20` through Homebrew and provide the exact Qt
version separately if the current Homebrew Qt differs:

```bash
brew install llvm@20
cmake --preset macos-debug -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/macos
cmake --build --preset macos-debug --parallel
ctest --preset macos-debug
open build-native/macos-debug/native/Relay.app
```

The Windows x64 presets are `windows-debug` and `windows-release`; configure
them from an x64 Visual Studio developer environment with C++ Build Tools/SDK,
LLVM 20 installed under `%ProgramFiles%/LLVM`, and the Qt MSVC 2022 x64 build.
`clang++` targets the MSVC ABI and dynamic runtime; the preset uses Microsoft's
`link.exe` to preserve valid Qt application manifests. Override
`-DCMAKE_CXX_COMPILER=...` for a different LLVM location. Use a fresh build
directory when changing compilers. Build artifacts stay under ignored
`build-native/` directories.

The native client deliberately reuses Electron's public metadata and GitHub CLI
locations, including `relay-data.json` and the sibling `github-cli/` directory.
Startup still clears the selected repository, credentials remain in `gh`'s OS
credential store, and the C++ presentation layer never receives a token.

Measurements and the exact commands used to collect them are recorded in
`docs/native-measurements.md`.

The [current native implementation and verification status](docs/native-completion-2026-09-14.md)
records the source changes, independent reviews and remaining release gates.
The native successor is not yet a verified replacement release.

Native development builds include application-menu **Settings**, multi-account
management and repository bindings, configurable diff text size and local Git
identity. History defaults to a normal commit list; Settings can switch to an
all-branch graph. Both modes share commit details and text/image previews.

Repository menus provide branch creation/rename/deletion, remote checkout,
merge, unpublished-commit rebase, stash/restore, recoverable selected-file
discard, revert/cherry-pick, latest-commit undo/message editing and local tags.
A conflict dialog handles resolution, continue, abort and skip. New repositories
can be initialized locally or published through an explicit GitHub dialog.
Production login/publication and installers still require native-platform checks.

A native [CI workflow](.github/workflows/native.yml) builds and tests pinned Qt
on macOS arm64 and Windows x64. Both platforms and the Apple Silicon source
installer passed for commit `ca05c2b` in [run 34118316989](https://github.com/ka-capek/relay-desktop/actions/runs/34118316989).
The latest local changes extend that workflow to validate the Windows source
installer and retain downloadable source-build artifacts for both platforms.
Those additions require a new CI run; earlier results do not validate them.

## Desktop builds

Release installers bundle their own Git and GitHub CLI runtimes. Those
third-party binaries are intentionally excluded from Git history; local release
packaging expects them under `runtime/git` and `runtime/gh`.

- `npm run desktop:mac` builds an Apple silicon DMG.
- `npm run desktop:windows` builds a Windows x64 installer.
- `npm run desktop:release` builds both.

## GitHub sign-in

Relay uses the official GitHub CLI browser/device OAuth flow. The CLI stores each
OAuth credential in the operating system credential store; Relay does not ask
for or persist personal access tokens. Relay keeps a separate GitHub CLI config
directory inside its app data so it does not change the active account in a
developer's normal terminal session.

Relay starts with no repository selected. Previously opened repositories remain
in the sidebar as recent shortcuts until the user chooses one.

## Repository workflow

- Clone from a searchable list of repositories available to the active GitHub
  account, including private and organization repositories, or paste an HTTPS or
  SSH URL.
- Use **File → Scan Folder for Repositories…** to recursively add existing local
  repositories in bulk.
- Remove a repository from the Relay sidebar with its trash button or the File
  menu. Relay only removes its shortcut and never deletes repository files.
- Change each account's commit email under **Manage accounts**. Leaving it blank
  restores that account's GitHub noreply address. Press Enter in the field or
  use **Save email**; both do the same thing.
- Order the sidebar manually with drag-and-drop, or sort it by the date a
  repository was added, by name, or by its latest commit. In manual mode the
  grip on each row can also be focused and moved with the arrow keys.

## History

The **History** tab loads the branch's commits progressively as you scroll,
with no fixed limit. Selecting a commit shows its full message, its author and
committer, its parents, any branch or tag decorations, the files it changed,
and the diff for any of them. A merge is shown against its first parent. You
can search the loaded commits by message, author, email, or hash, copy the full
hash, and open the commit on GitHub when `origin` is a GitHub remote.

## Non-GitHub hosts over SSH

Relay's accounts are GitHub.com identities. For any other Git host, add an
**SSH identity** under **Repository account settings** and bind it to a
repository, or choose one while cloning an SSH URL.

If your SSH agent and `~/.ssh/config` already work, you need none of this;
Relay changes nothing by default. An identity is only useful when the default
is not enough, such as several accounts on one host, and it does no more than
choose which key is offered.

Relay stores a host and, optionally, the path to a key. It never reads, copies,
or stores private keys or passphrases; those stay with OpenSSH and your agent.
A GitHub token is never sent to a host other than github.com.
