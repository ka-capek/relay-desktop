# Relay

Relay is a small GitHub Desktop-style client focused on easy multi-account
switching. Relay 0.5.0 is the shipping Electron client for Apple silicon Macs
and 64-bit Windows. A C++26/Qt 6 native successor now lives alongside it under
`native/`; the Electron client remains the parity specification until the
native release passes its cross-platform stop/go gate.

## Download

Download the ready-to-use installers from the
[latest GitHub release](https://github.com/ka-capek/relay-desktop/releases/latest):

- Windows 64-bit: `Relay-Setup-0.5.0-x64.exe`
- macOS Apple Silicon: `Relay-0.5.0-arm64.dmg`

The builds are currently unsigned. On macOS, right-click Relay in Applications
and choose **Open** the first time; Windows SmartScreen may also ask for
confirmation. The macOS app is ad-hoc signed during packaging, which is what
stops Apple Silicon reporting it as damaged.

## Prerequisites

- Node.js `>=22.13.0` (development only)
- Git and the official GitHub CLI when running from source

## Quick Start

```bash
npm install
npm run desktop:open
```

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

The [current native implementation and verification status](docs/native-completion-2026-09-07.md)
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

A native [CI workflow](.github/workflows/native.yml) is prepared for pinned Qt
builds and tests on macOS arm64 and Windows x64. Its presence is not evidence
that those jobs have passed.

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
