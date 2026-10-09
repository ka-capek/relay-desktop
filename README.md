# Relay

Relay is a small GitHub Desktop-style client focused on easy multi-account
switching. It is a native C++26/Qt 6 application for Apple Silicon Macs and
64-bit Windows.

The earlier Electron client (Relay 0.5.0) is no longer maintained and has been
removed from this repository. The native client uses the same data folder, so
accounts, repositories and settings carry over with no new sign-in.

## Download

[**Relay 0.5.4**](https://github.com/ka-capek/relay-desktop/releases/tag/v0.5.4)
is a prerelease. The Windows setup includes Git for Windows and the GitHub CLI;
the macOS DMG includes the GitHub CLI and uses the Git from the Xcode Command
Line Tools or Homebrew. The installers are unsigned (macOS uses ad-hoc
signing) and not notarized. Real multi-account OAuth, private clone/fetch/push
and publication still need installed-platform verification.

On macOS, right-click Relay Native in Applications and choose **Open** the
first time; Windows SmartScreen may also ask for confirmation.

## Installers and upgrades

Prereleases provide a Windows **Relay-Native-Setup-<version>-x64.exe**
and an Apple Silicon **Relay-Native-<version>-arm64.dmg**, with a
`SHA256SUMS.txt`. These are unsigned preview distributions with the bundled
tools described above. Pushing a `v<version>` tag builds, tests and publishes
them from CI.

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

## Install from source on Apple Silicon

On macOS 14 or newer, download and run the installer:

```bash
curl -fL https://raw.githubusercontent.com/ka-capek/relay-desktop/main/native/tools/install-macos.sh -o /tmp/relay-install-macos.sh
bash /tmp/relay-install-macos.sh
```

It prepares Homebrew dependencies, downloads `main`, builds
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

## Install from source on Windows x64

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
build, not a signed release. See the
[completion record](docs/native-completion-2026-09-14.md) for verification status.

## First-run setup

Relay checks Git and GitHub CLI on startup without signing in. Missing or old
tools produce a persistent banner with installation guidance. Choose **Check
again** after installing, or **Help → Check Git and GitHub CLI** at any time.
The supported baselines are Git 2.35.0 and GitHub CLI 2.98.0 (the previous bundled
CLI version). Local repositories remain usable without a GitHub account.

Windows also checks the standard Git and GitHub CLI installation directories
when an already-running app has an old PATH. A custom installation directory
must already be on the app's PATH; otherwise restart after updating PATH.

## Sync, diff sizing and SSH selection

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

## Branches and ordinary Git operations

The current-branch picker lists local and fetched remote-tracking branches.
Selecting a remote branch checks it out as a local tracking branch; remote
branches that already have a local branch of the same name are not listed
again. A branch that so far exists only on origin appears after **Fetch all
branches from origin**, the last item of the picker. In History,
use the branch selector to browse a local branch, a remote branch, or all
branches together without changing the working tree. **Check out branch** is
an explicit action; **New branch from here** starts a branch at the selected
branch tip. **Fetch all origin branches** discovers origin branches even when
the repository was cloned with a single-branch fetch configuration. It does
not change that configuration or prune old remote-tracking refs. Branches
already fetched from other remotes are also visible.

Repository actions include branch creation/rename/local deletion, merge,
rebase, revert, stash/apply/drop, tags, recoverable discard, undo,
amend and conflict resolution with continue/skip/abort. Select multiple history
rows with Ctrl/Cmd or Shift, then **Repository → Cherry-pick selected commits…**.
The confirmation shows the destination and applies the selected commits from
oldest to newest in displayed history order (up to 200). A conflicting series
can be continued, skipped, or aborted as one operation. Merge commits use their
first-parent changes.

Add co-authors under the commit description as `Name <email>`, separated by
commas, or pick a recent author of the repository with **Recent**. Relay adds
them as `Co-authored-by` trailers, which GitHub shows on the commit.

**Repository → Push tag to origin…** publishes a local tag; a different tag
with the same name on origin is never replaced. **Delete tag on origin…** lists
origin's tags and removes the chosen one, leaving local tags as they are.

**Repository → Open in editor / Open in Terminal / Show in Finder** (Explorer on
Windows) open the repository folder in another application. Choose the editor
in Settings → General; Relay detects Visual Studio Code, Cursor, Zed, Sublime
Text, JetBrains IDEs and others, or any application you pick.

**Repository → Compare branches…** shows the commits only on each of two local
or remote branches and the files the compared branch changed since it branched
off, with diffs. When the base is the current branch it can merge the other
branch in.

**Repository → Squash and reorder commits…** lists the commits that are not on
any remote yet. Drag them into a new order, check a commit to squash it into
the one below, and edit messages; Relay then rewrites them with an interactive
rebase. Published commits are never offered, and a conflict can be continued,
skipped or aborted like any rebase.

**Repository → Create or open pull request** opens the current branch's open
pull request on GitHub.com, or GitHub's page for creating one. Unpushed commits
can be pushed first.

**Repository → Delete branch on origin…** removes a fetched branch from origin.
The confirmation shows its last commit so it can be restored, local branches
are kept, origin's default branch is refused, and Relay refuses the deletion if
someone pushed to the branch after your last fetch.

**Force push origin…** (Repository menu and the sync button's menu) replaces
origin's copy of the current branch, for example after amending or rebasing
commits that were already pushed. The confirmation shows the origin commit from
your last fetch, and the push is refused if origin has moved since, so work
someone else pushed in the meantime is never overwritten unseen.

## Gitea, Forgejo and GitLab accounts

Open the account menu → **Gitea / GitLab accounts and repositories…**.
Choose the server type and HTTPS server address (GitLab.com or a self-hosted
instance, including installations under a URL subpath). Use **Open token
settings in browser**, sign in and create a read-only API token: Gitea/Forgejo
needs read:user and read:repository; GitLab needs read_api. GitHub retains its
existing browser/device login. Direct OAuth for arbitrary third-party servers
requires a registered application and is not configured in this build.

Relay lists the repositories available to the connected account, including
private, read-only and archived repositories, with pagination and filtering.
GitLab listing covers projects the account belongs to. Multiple accounts on
different servers or the same server stay separate. Open a repository in the
browser or choose **Clone with SSH…** and an SSH profile for its transport host.
The API token is used for discovery only; Git operations use SSH identities
or the existing Git credential configuration. SSH-only servers work through
normal Git URLs and local repositories without a provider API account.

API tokens are stored in macOS Keychain or Windows Credential Manager, never
in relay-data.json. Disconnecting removes the account and its saved token;
failed vault cleanup can be retried and is retried on startup. Tokens remain
bound to their provider/server/account even if public metadata is edited.
The Linux development harness uses an injected test vault; no plaintext
credential fallback is provided. Real account login and private operations
still need installed-platform testing against your own server.

## Themes and branch graph

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

## Building from source


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

Relay keeps the public metadata and GitHub CLI locations used since 0.5.0,
including `relay-data.json` and the sibling `github-cli/` directory.
Startup still clears the selected repository, credentials remain in `gh`'s OS
credential store, and the C++ presentation layer never receives a token.

Measurements and the exact commands used to collect them are recorded in
`docs/native-measurements.md`.

The [current native implementation and verification status](docs/native-completion-2026-09-14.md)
records the source changes, independent reviews and remaining release gates.

Development builds include application-menu **Settings**, multi-account
management and repository bindings, configurable diff text size and local Git
identity. History defaults to a normal commit list; Settings can switch to an
all-branch graph. Both modes share commit details and text/image previews.

Repository menus provide branch creation/rename/deletion, remote checkout,
merge, rebase, stash/restore, recoverable selected-file
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

To regenerate the application icons after editing `build/icon.svg` or
`build/icon-small.svg`, run `cmake --build --preset <preset> --target icons`.

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
- Pane widths and the window size are remembered; **View → Reset Layout**
  restores the defaults.
- When a pull finds that both your branch and origin have new commits, Relay
  asks whether to merge or to rebase your commits.
- **Repository → Rebase current branch…** warns when the rebase would rewrite
  commits already on a remote branch, suggests merging instead, and after a
  confirmed rewrite offers the protected force push.
- Each account can sign its commits with its own SSH key (account menu →
  **Commit signing**). Commit details show when a commit is signed.

## History

The **History** tab loads the branch's commits progressively as you scroll,
with no fixed limit. Selecting a commit shows its full message, its author and
committer, its parents, any branch or tag decorations, the files it changed,
and the diff for any of them. A merge is shown against its first parent. Search
covers the whole history, not only what is loaded: message (including the
body), author, email, or hash. You can copy the full hash and open the commit
on GitHub when `origin` is a GitHub remote.

## Non-GitHub hosts over SSH

For any Git host, add an **SSH identity** and select it for a repository from
the account menu, or choose one while cloning an SSH URL.

If your SSH agent and `~/.ssh/config` already work, you need none of this;
Relay changes nothing by default. An identity is only useful when the default
is not enough, such as several accounts on one host, and it does no more than
choose which key is offered.

Relay stores a host and, optionally, the path to a key. It never reads, copies,
or stores private keys or passphrases; those stay with OpenSSH and your agent.
A GitHub token is never sent to a host other than github.com.
