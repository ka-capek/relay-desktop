# Relay Desktop: Coding Agent Guide

This document is the authoritative engineering guide for coding agents working
on Relay. Read it before changing code, running release commands, modifying Git
or GitHub authentication, or publishing anything.

Relay is a native C++26/Qt 6 Widgets application for macOS and Windows. The
Electron client (`0.5.0`) has been removed from this repository and is no
longer maintained. When this document and the code disagree, treat the code as
the immediate source of truth and update this document in the same change.

For the latest verification record read `docs/native-completion-2026-09-14.md`.
Commit `ca05c2b` passed the pinned Qt macOS and Windows CI jobs, including the
macOS source installer, in run 34118316989.

## 1. Product Definition

Relay is a small GitHub Desktop-style client whose distinguishing feature is
first-class support for multiple accounts.

The supported production targets are deliberately narrow:

- macOS on Apple Silicon (`arm64`)
- Windows 64-bit (`x64`)
- GitHub.com for OAuth accounts; Gitea, Forgejo and GitLab (including
  self-hosted HTTPS servers) for API repository discovery; any Git host over
  SSH

Relay provides these flows:

- Connect multiple GitHub accounts using the official GitHub CLI browser/device
  OAuth flow, and switch between them without a personal access token.
- Assign a specific account to a specific local repository, or let that
  repository follow the globally active account.
- Set a distinct commit email for each connected account.
- Open, initialize, or publish a local repository; recursively scan a folder
  for repositories; remove a shortcut without touching disk.
- Browse and clone repositories available to a GitHub, Gitea/Forgejo or GitLab
  account, or clone an arbitrary HTTPS or SSH URL.
- View working-tree changes, textual diffs and bounded image previews.
- Browse history as a list or an all-branch graph, scoped to the current
  branch, all branches, or a chosen local or remote branch.
- Commit selected files; fetch, pull (fast-forward), push and lease-protected
  force push to `origin`; delete branches on `origin`.
- Create or open the current branch's GitHub pull request in the browser.
- Squash, reorder and reword commits that are not on any remote yet.
- Compare two local or remote branches and merge the compared branch.
- Open the repository in an external editor, Terminal, or Finder/Explorer.
- Add co-authors to a commit; push tags to origin and delete tags on origin.
- Switch to local or remote-tracking branches; create, rename and delete local
  branches; merge, rebase (published commits only after a warning), revert, cherry-pick (including
  several commits), stash, recoverable discard, undo, amend, tags, and conflict
  resolution.
- Select an SSH identity per repository for any SSH host, including GitHub.
- Choose a theme, including imported custom palettes.

Relay is not a complete GitHub Desktop replacement. See "Known Limitations"
before promising or assuming behavior.

## 2. Non-Negotiable Product Invariants

Preserve these behaviors unless the user explicitly requests a product change:

1. **Start with no repository open.** Recent repositories may remain in the
   sidebar, but application startup must not automatically reopen one.
2. **Never ask for a GitHub PAT.** GitHub authentication remains browser-based
   GitHub CLI OAuth. The owner explicitly authorized API-token fallback for
   Gitea/Forgejo/GitLab discovery on 2026-09-25. Only the one-time password
   input may hold that token; keep it out of account models, JSON and logs,
   and store it in macOS Keychain / Windows Credential Manager.
3. **Never persist an OAuth token in `relay-data.json`, widget or model state,
   logs, or Git configuration.** Tokens may exist only transiently inside a
   controller worker operation and a child process environment.
4. **Removing a repository from Relay never deletes, moves, cleans, resets, or
   otherwise modifies repository files.** It removes only Relay metadata.
5. **Repository identity follows this precedence:** an explicit per-repository
   account binding first, otherwise the globally active account.
6. **Account commit email is independent of the GitHub login email.** A blank
   value resets to GitHub's numeric-ID noreply address.
7. **The account menu closes when the user clicks outside it.**
8. **Use flat, code-native SVG icons.** Do not introduce emoji as UI icons,
   glossy/illuminated icons, or icon gradients.
9. **Keep the signed-in status visually quiet.** No green status dot and no
   "All systems operational" message.
10. **macOS and Windows have equivalent menu functionality.** Windows uses the
    in-window `QMenuBar`; macOS uses the system menu bar with Qt menu roles.
11. **The presentation layer never receives a token.** Widgets and item models
    receive only sanitized domain values from `RelayController`; tokens stay
    transiently inside the controller/service operation that launches Git or
    calls an API, and no signal carries one.
12. **External navigation is HTTPS-only and opens in the system browser.**

These invariants are partly covered by tests, but several remain behavioral and
must also be checked manually.

## 3. Repository Identity and Publication

- Public repository: <https://github.com/ka-capek/relay-desktop>
- Default branch: `main`
- Version: `project(RelayNative VERSION ...)` in `CMakeLists.txt`, currently
  `0.5.4`. CI names installers from it.
- Releases so far are prereleases (`v0.5.1` to `v0.5.4`). They use
  external Git/gh and do not satisfy the stable bundled-runtime distribution
  gate. `v0.5.0` was the last Electron release.
- Release page: <https://github.com/ka-capek/relay-desktop/releases>
- Product name `Relay`; installed app `Relay Native.app` /
  `%LOCALAPPDATA%/Programs/Relay Native`

Do not push, publish a release, change repository visibility, or modify GitHub
settings unless the user explicitly authorizes the remote write.

## 4. Technology Stack

- C++26, required with compiler extensions disabled
- Qt 6.11.1 exactly: Core, Gui, Widgets, Network, Svg, Concurrent, and Test
- CMake 3.30+ with Ninja and checked-in platform presets
- Upstream LLVM 20 `clang++` for the presets: Homebrew `llvm@20` on macOS,
  `%ProgramFiles%/LLVM` on Windows with an x64 Visual Studio developer environment.
  Windows uses the MSVC target, Microsoft `link.exe`, and dynamic CRT matching
  Qt's MSVC binaries. LLVM 20's implicit linker produced invalid namespace-qualified
  UAC attributes in the merged Qt manifest, preventing process startup;
  CMake 3.31 does not map C++26 for AppleClang, cl.exe or clang-cl.
- Dynamically linked Qt under LGPLv3; Relay's own code remains MIT
- The same bundled/system Git and official GitHub CLI child-process model
- The `relay-data.json` and isolated `github-cli/` locations used since 0.5.0
- Model/view widgets for every unbounded list, including a virtualized table
  diff rather than a widget per line

The code lives under `native/`.

## 5. Architectural Overview

```text
Qt Widgets views and item models
        |
        | sanitized domain values and queued signals
        v
RelayController
        |                 |                    |
        v                 v                    v
GitService          GitHubAuth/API       RelayStore/discovery
(QProcess, worker   (`gh`, Qt Network,   (legacy-compatible JSON,
threads)             no token signals)   atomic writes, BFS)
        |                 |
        v                 v
Bundled/system Git   OS credential store through gh
```

Synchronous service methods are headless building blocks and must run in worker
threads when called from the application. `RelayController` owns request
generations and drops stale repository, diff, history, and commit responses.
`AsyncProcess` provides direct cancellation for single-process vertical slices.
Widgets never call Git, `gh`, the filesystem store, or GitHub REST directly.

The native clone browser uses a separate request generation: only the newest
account-repository lookup may publish success or failure. Changing the dialog's
account clears its previous repository selection before requesting another
list. `runAsync` checks optional freshness predicates before reading a future
and unwraps `QUnhandledException` so service error messages survive QtConcurrent.

### 5.1 Historical native continuation and review, 2026-09-06

The owner reaffirmed C++/Qt, GitHub Desktop-style interaction, and multiple
accounts. A normal commit list is the first milestone; a branch graph is a
later optional history mode. Do not expose a nonfunctional graph toggle.
Five independent adversarial reviews and their disposition are recorded in
`docs/native-review-2026-09-06.md`.

Native behavior recorded in that review:

- Settings is reachable from Edit on Windows/Linux and the application menu
  on macOS via `QAction::PreferencesRole`. General settings persist
  `preferences.refreshOnFocus` (default true) and `preferences.diffFontSize`
  (default 12, range 10–24) in the existing public JSON store. Accounts links
  to account management. Unknown preference fields survive writes.
- Edit actions target the focused text editor or diff. History and file
  selection work with keyboard navigation; history refreshes on branch/HEAD
  changes and clears obsolete details. Draft commit messages are kept per
  repository for the current application session.
- Repository → New branch creates and switches to a validated local branch.
  Pull origin fetches then fast-forwards the configured origin upstream. It
  refuses divergent history rather than silently creating a merge. Full merge
  and conflict resolution UI is not implemented.
- Repository mutations are serialized, refresh cannot invalidate an active
  mutation, and stale repository read failures are suppressed. Successful
  clones retain recents and account bindings even if selection changes.
- Persistence failures restore the previously saved public state. Repository
  activation happens only after its metadata is saved. Corrupt/unreadable
  stores produce an error and are preserved, rather than silently becoming
  empty stores that could overwrite account bindings.
- Git status/name-status/numstat use NUL-delimited records. UI-selected paths
  are literal pathspecs; a rename includes both paths, and pre-staged deletions
  can be committed without staging an already absent path. Diff content keeps
  significant trailing whitespace. Untracked text previews are limited to
  2 MiB; the process runner fails on output overflow rather than silently
  returning a truncated successful result. Git operations time out after
  120 seconds.
- Authentication discovers bundled `gh` first. Missing/invalid/expired CLI
  account responses preserve existing settings. Login is cancellable and
  bounded to 15 minutes. SSH does not require a GitHub OAuth token. The
  command-scoped credential helper responds only to HTTPS github.com `get`
  requests and receives account values as environment data, never shell text.
  Push identity routing uses the origin push URL; multiple push URLs are
  explicitly unsupported. SSH profile usernames apply to actual transport.
- macOS stays alive when its window closes and reopens on application
  activation; Quit exits explicitly. Smoke mode uses a disposable profile.
- CMake presets use schema 8 compatible with the declared 3.30 minimum and
  disable unused C++ module scanning. NSIS shortcuts target the installed
  executable directory. Stage verification excludes `otool` headers from
  dependency checks and does not mistake ordinary directories for forbidden
  payloads.

Local Linux/Qt 6.8.2 tests supplement, but do not replace, the required native
macOS arm64/Windows x64 Qt 6.11.1 builds and packaged authentication checks.

### 5.2 Current native workflows, 2026-09-07

`native/tools/install-macos.sh` is the local Apple Silicon installer (macOS
14+). It uses isolated cached tools/Qt, a fresh temporary release build, Qt
framework deployment, ad-hoc signature validation and a disposable-profile
startup test before replacing `~/Applications/Relay Native.app`. Existing apps
are retained as dated backups; the user's checkout and repositories are never
reset or cleaned. Git/gh remain external Homebrew dependencies, exposed to
Finder launches through the app's `LSEnvironment` PATH. It never changes shell
profiles or signs in to accounts. CI exercises the installer from its checkout
and the installed app through Launch Services. The script is not a substitute
for the strict redistribution/release packaging pipeline.

The latest inventory and release limits are in
`docs/native-completion-2026-09-07.md`; it supersedes the feature inventory in
section 5.1. Native additions live in `git_workflows.cpp` and `workflow_ui.cpp`
with the same controller/service boundary as existing operations.

- GitHub account and SSH profile bindings resolve canonical repository paths
  and existing legacy aliases (including macOS `/var` versus `/private/var`).
  Updating a binding removes aliases of that repository only; unavailable paths
  remain preserved in metadata.
- `Preferences` now includes `commitName`, `commitEmail` and `graphHistory`.
  The all-branch graph is the default since 0.5.5. Graph history snapshots local/remote branch tips,
  sends revision lists over stdin and batch-validates them. Filtering does not
  draw misleading edges across hidden commits.
- `RepositoryAction` covers local branch rename/delete/remote checkout, merge,
  unpublished rebase, conflict resolve/continue/abort/skip, stash/apply/drop,
  recoverable discard, revert/cherry-pick, undo, message amend, tags and origin.
  Git account identity is explicit for commit-producing actions; without an
  account, Settings and effective Git configuration supply a displayed identity.
- Discard retains a full recovery stash, reapplies its index/worktree, then
  restores only the expanded selected paths. Before any stash, refuse tracked
  files replaced by real directories; Git can otherwise delete ignored content
  not included in the stash. Use a unique recovery message to avoid identical
  stash-object collisions. Never replace this with `stash --all` automatically.
- Do not globally enable literal pathspecs for stash commands. Git's internal
  cleanup depends on generated magic pathspecs. Other user path arguments remain
  literal. Apply stashes with `--index`, and retain the stash until explicit drop.
- Known published commits cannot be undone/amended here; rebasing them needs
  the explicit `rebasePublished` confirmation (section 12.6). Local branch
  deletion additionally requires containment in the current branch. Force
  push exists only in its lease-protected form (section 12.6). Merge commits revert/cherry-pick against their first parent.
- File previews are worker-produced `FilePreview` values. Widgets never read
  files. Raster images are bounded to 10 MiB encoded and 16 megapixels decoded.
  Symlinks do not dereference to target content. Decode failures must not be
  labeled as an absent file side; selection changes clear old image previews.
- A publication dialog captures the displayed account and defaults to private.
  Its worker verifies GitHub's authenticated login before creating a repository.
  Persist identity binding before the remote write. Keep partial-success and
  uncertain-POST recovery messages; never automatically retry or delete remotes.
  API traffic is HTTPS to api.github.com with redirects disabled and size/time
  limits. Origin editing rejects embedded credentials and explicit push URLs
  requiring separate handling.
- Account and repository mutation locks exclude each other. Accepted account
  changes immediately invalidate previous discovery. Opening/creating a repo
  during a mutation is rejected before invalidating generations.
- MainWindow displays the actual branch until a requested switch succeeds.
  Conflict errors stay inside the modal dialog. Undo restores the previous
  message only when it would not overwrite a user's existing draft.
- `.github/workflows/native.yml` is source-only preparation for macOS/Windows
  checks. It has not run in this environment. No installer, signing, remote
  publication, or full upstream-feature-parity claim follows from Linux tests.

Windows CI uses aqt's `--external` 7-Zip backend: py7zr rejected the Qt
`modules/SvgWidgets.json` link during parallel SDK extraction in run
34117355064. Run extraction sequentially with `aqt-windows.ini` and precreate
the SDK directory: parallel 7-Zip workers also raced creating that directory
in run 34117737965. Keep download verification and fail if 7-Zip is unavailable.

The source installer includes verbatim GNU license texts from `native/licenses/`
so an outage at gnu.org cannot interrupt an otherwise complete installation.

### 5.3 Native themes and graph colors

`theme.cpp` applies a semantic palette to QPalette, QSS and painted delegates.
Six bundled JSON palettes under `native/resources/themes/` provide Light,
Dark, Catppuccin Latte and Mocha, and the high-contrast Mono Light and Mono
Dark. Native preferences persist `themeId` and a
`customTheme` JSON object. Appearance settings support base selection, editable
overrides and import/export. Validate unknown keys, color syntax and branch
palette length before saving/importing. Export resolved colors using QSaveFile;
never store a dependency on the imported path or execute arbitrary theme code.
Invalid persisted overrides fall back to the selected base; legacy settings use
Light. Palette changes repaint existing controls and diffs without a restart.
Qt Fusion and explicitly positioned licensed SVG chevrons avoid platform/QSS
arrow placement conflicts; native menu roles remain intact. The shared core
owns resources so application and UI tests use the same icons and palettes.

Since 0.5.5 the design is square and dense: no rounded corners anywhere
(the theme test rejects any non-zero `border-radius`), hairline borders,
12px body text, 24px single-line rows for files, commits and repositories,
square badges and commit marks. Splitters keep a 5px grab area drawn as a 1px
line. Every button, field and combo is exactly 24px with no vertical padding.
Panels share one grid: the sidebar heading copies the tab bar's height
(`MainWindow::eventFilter`), header rows use 5px margins with zero layout
spacing, so the sidebar's filter/order rows, the History header rows, the
Changes header, the Diff label and the commit detail header sit on the same
lines. The history graph uses one column as wide as the widest loaded row, so
every message starts at the same x; the list never scrolls sideways. An empty
`DiffView` takes no width and shows nothing; only a file without a text diff
shows the notice. Repository selection stays a tint without a left-edge marker. Every item
view has mouse tracking so delegates' hover state follows the pointer.

History is a one-line-per-commit table: graph, ref chips, subject, then fixed
author/hash/date columns sized by the view (narrow views drop author, then
hash). Its header has two rows: branch scope, List/Graph, Check out, Branch
from…, Fetch all and **Focus** (hides the sidebar and commit details, full
screen; Esc leaves); then the search field selector (All, Message, Author,
Branch, Hash), the query, **Highlight**, a match counter and previous/next
(Enter / Shift+Enter). Highlight (the window's default) keeps every loaded
commit and the graph, tints matches and dims the rest through
`HistoryCommitListModel::highlightRole`; turning it off filters, and only
filtering asks Git to search beyond the loaded pages. Branch search matches
the commits reachable from tips whose refs match, within loaded history.
The model's own default stays filtering for API compatibility.

History has a visible List/Graph selector. Graph colors belong to active lines
of ancestry, independently of lane position, and survive pagination/compaction.
Allocate unused color identities until a line ends; do not recolor a passing
line when a neighboring branch finishes. First-parent connectors retain the
child's color until joining; merge arms use their parent line colors. Graph
mode omits day-header gaps (dates remain in metadata) and filtered results hide
edges. Dots and reference chips share graph colors, with hollow merge dots.
Tests cover concurrent tips, compaction, paging, preference persistence,
malformed themes and screenshots of every preset's graph, diff and settings.

## 6. Authoritative File Map

Native source installation now also has `native/tools/install-windows.ps1`
and `install_windows.py`. The PowerShell entry selects the x64 Visual Studio
environment; Python builds with pinned tools, deploys dynamic Qt and the MSVC
CRT, smoke-tests with a clean runtime PATH, and publishes through same-volume
renames. Keep the previous installation and restore it if publication fails.
Reject unrelated existing destinations and running Relay processes. Only the
new stage/cache may be cleaned. Git and gh are external; this workflow is not
the strict redistribution pipeline or a signed release. Changes require the
Windows installer safety tests and the native CI installation smoke test.
The shared aqt helper accepts `--output-dir` without GitHub environment files.
The Windows installer discovers the single complete `Microsoft.VC*.CRT` payload
under the active `VCToolsRedistDir/x64`, rather than assuming VC143; missing or
ambiguous runtimes fail before installation.

`runtime_check.cpp` supplies startup dependency checks (Git >=2.35.0 and
gh >=2.98.0, a conservative baseline matching the previously bundled CLI).
Checks use the actual service executable/environment and bounded `--version`
processes in a controller worker; do not authenticate, install tools, or persist
their output. Widgets get only sanitized issue strings, shown in a persistent
selectable banner with retry and a Help-menu entry. Missing CLI startup must
preserve existing account metadata. Windows rechecks standard external tool
locations when PATH has not yet changed in the current process. An external
absolute Git path must not activate the bundled-Git environment overrides.
Both repository sorting implementations use an English collator only when the
default locale is C, where Qt otherwise ignores numeric mode.

| Path | Purpose |
| --- | --- |
| `CMakeLists.txt`, `CMakePresets.json` | C++26 project, Qt dependency pin, macOS arm64 and Windows x64 presets |
| `native/include/relay/domain.hpp` | Sanitized shared domain models and legacy JSON conversion |
| `native/src/process_runner.cpp` | Bounded child-process execution plus cancellable asynchronous operations |
| `native/src/git_service.cpp` | Git commands, parsers, history, diffs, mutations, and transport routing |
| `native/src/github_auth.cpp` | Isolated `gh` account discovery, login, switching, logout, and transient token lookup |
| `native/src/github_api.cpp` | Profile, email, and pushable-repository REST calls without exposing tokens to views |
| `native/src/relay_store.cpp` | Atomic, owner-only metadata persistence, readable by older Relay versions |
| `native/src/repository_discovery.cpp` | Bounded non-destructive breadth-first repository scanning |
| `native/src/repository_order.cpp` | Ordering normalization, sorting, and manual-order compatibility |
| `native/src/ssh_service.cpp` | SSH profile parsing (including GitHub), host-checked commands, and connection tests |
| `native/src/avatar_cache.cpp` | Restricted-host, size-bounded, offline avatar cache |
| `native/src/diff_model.cpp`, `native/src/diff_view.cpp` | Virtualized accessible unified-diff parser and table view |
| `native/src/list_models.cpp` | Repository, file, history, and commit-file models |
| `native/tests/` | QtTest unit, integration, large-diff, Git-fixture, and app smoke tests |
| `docs/native-measurements.md` | Reproducible native size and physical-footprint measurements |
| `native/src/git_workflows.cpp`, `native/src/workflow_ui.cpp` | Repository actions (branches, merge, rebase, stash, discard, cherry-pick, tags) and their UI |
| `native/src/forge_service.cpp`, `forge_controller.cpp`, `forge_dialog.cpp`, `forge_ui.cpp` | Gitea/Forgejo/GitLab API discovery and account UI |
| `native/src/credential_store.cpp` | macOS Keychain / Windows Credential Manager access for forge API tokens |
| `native/src/runtime_check.cpp` | Startup Git and GitHub CLI version checks |
| `native/src/theme.cpp`, `native/resources/themes/` | Semantic palette, QSS, and bundled theme JSON |
| `native/src/main_window.cpp`, `native/src/dialogs.cpp` | Main window, menus, and dialogs |
| `native/tools/` | Source installers, preview DMG, Windows installer tests, release verification |
| `native/packaging/` | Strict packaging guide, notices, and the Inno Setup preview script |
| `.github/workflows/native.yml` | macOS arm64 and Windows x64 build, test, installer and artifact CI |
| `build/icon.svg`, `build/icon-small.svg` | Icon masters (64px and above; 48px and below) |
| `build/icon.icns`, `build/icon.ico`, `build/icon.png` | Generated icons used by the bundle, `.rc`, CPack and Inno Setup |
| `PLAN.md`, `docs/` | Rewrite plan, reviews, completion records and measurements |
| `TODO.md`, `TODO-Karels.md` | Requested backlog; items are not done until verified |

### Generated and local-only paths

| Path | Treatment |
| --- | --- |
| `build-native/` | CMake build trees; never commit |
| `outputs/` | Installers and measurements; publish as release assets, not Git files |
| `work/` | Local integration fixtures and temporary profiles; never publish |
| `runtime/` | Large third-party Git and GitHub CLI runtimes; intentionally ignored |

## 7. Startup and Application Lifecycle

1. `main()` constructs `RelayApplication` and applies the saved theme.
2. With `--smoke-test`, a disposable profile replaces the real store and
   account synchronization is skipped.
3. `RelayController` and `MainWindow` are created and the window is shown.
4. The controller reads the store, runs the Git/gh runtime check, and
   synchronizes GitHub accounts on a worker thread.
5. No repository is opened; `selectedRepositoryPath` is always `null`.

On macOS the app stays alive when its window closes
(`setQuitOnLastWindowClosed(false)`) and shows the window again on application
activation; Quit exits explicitly. On Windows closing the window quits.

When the window is activated, Relay refreshes the open repository if
`preferences.refreshOnFocus` is on and no operation is busy.

## 8. Persistence Model

Relay stores public application metadata in:

```text
<user data>/relay-data.json
```

Typical locations are:

- macOS: `~/Library/Application Support/relay-desktop/relay-data.json`
- Windows: `%APPDATA%\relay-desktop\relay-data.json`

The GitHub CLI config for Relay lives beside it:

```text
<user data>/github-cli/
```

The metadata store has this conceptual shape:

```json
{
  "accounts": [
    {
      "id": "github-96548046",
      "githubId": 96548046,
      "name": "Example User",
      "handle": "example",
      "email": "96548046+example@users.noreply.github.com",
      "initials": "EU",
      "status": "Example User",
      "tone": "coral",
      "authSource": "github-cli",
      "tokenSource": "credential store",
      "active": true
    }
  ],
  "activeAccountId": "github-96548046",
  "repositories": [
    {
      "path": "/absolute/path/to/repository",
      "name": "repository",
      "owner": "owner",
      "branch": "main",
      "changes": 2,
      "lastOpened": "2026-08-25T12:00:00.000Z"
    }
  ],
  "selectedRepositoryPath": null,
  "repositoryAccounts": {
    "/absolute/path/to/repository": "github-96548046"
  },
  "repositoryOrder": { "mode": "manual", "direction": "asc" },
  "manualOrder": ["/absolute/path/to/repository"],
  "sshProfiles": [
    {
      "id": "ssh-gitlab.example.com-1756150000000",
      "label": "Work GitLab",
      "host": "gitlab.example.com",
      "user": "git",
      "port": null,
      "identityFile": "/Users/example/.ssh/id_ed25519",
      "identitiesOnly": true
    }
  ],
  "repositorySshProfiles": {
    "/absolute/path/to/repository": "ssh-gitlab.example.com-1756150000000"
  }
}
```

Repository summaries also carry `addedAt`, the time Relay first remembered the
repository, and `latestCommit`, the committer date of the current `HEAD` or
`null` in a repository with no commits. Both exist to drive sidebar ordering.

Important persistence details:

- `writeStore()` writes a temporary file and renames it, reducing the chance of
  a partially written JSON file.
- File mode `0600` is requested where supported.
- Store reads merge with `emptyStore()` for simple forward compatibility.
- Legacy `encryptedToken` fields are removed during reads.
- Only accounts whose `authSource` is `github-cli` survive normalization.
- `selectedRepositoryPath` is forced to `null` on every read and in every public
  state response. This is how Relay guarantees a no-repository startup.
- Recent repository summaries are capped at 5,000.
- Removing an account removes stale per-repository bindings during the next
  account synchronization.
- Custom account emails survive normal account synchronization because known
  account metadata is reused.

Ordering and SSH fields are normalized on every read by
`RelayStore` and `repository_order.cpp`. A store
written before those fields existed is upgraded in place: `repositoryOrder` and
`manualOrder` gain defaults, `manualOrder` is seeded from the existing
repository list so nothing is reshuffled, and `addedAt` backfills from
`lastOpened`, which is never later than the true first-added time. Nothing is
removed, so an older Relay can still read the file.

An SSH profile holds a host, an optional user and port, and an optional *path*
to a private key. It never holds key contents or a passphrase.

An account may carry `signingKey`, the absolute path of an SSH key that signs
its commits; it is preserved by account synchronization and omitted when
empty. `preferences.layout` holds the window geometry and splitter states
(base64 from Qt). Only `RelayController::saveLayout()` changes it, without
publishing state, so a settings change never overwrites it.

Do not put secrets into this store. If a new field is sensitive, it does not
belong here.

## 9. Account and Credential Model

### 9.1 Login

`connectAccount()` invokes:

```text
gh auth login
  --hostname github.com
  --git-protocol https
  --web
  --clipboard
  --skip-ssh-key
```

GitHub CLI runs without an interactive terminal inside Relay. In that mode it
prints the official device URL rather than opening it. Relay parses the one-time
device code from cleaned CLI output (`GitHubAuth::loginProgressFromOutput()`),
emits only the sanitized code and fixed verification URL through the
controller's `loginProgress` signal, and the window opens
`https://github.com/login/device` once per login attempt. Login is cancellable
and bounded to 15 minutes.

The OAuth credential is stored by GitHub CLI in the operating system credential
store. Relay's JSON store contains only account metadata.

### 9.2 Account synchronization

`RelayController::synchronizeAccounts()`:

1. Reads authenticated GitHub CLI accounts.
2. Reuses known metadata by case-insensitive handle.
3. Fetches `/user` only for newly observed accounts.
4. Builds a stable account ID from the numeric GitHub user ID.
5. Selects the CLI-active account, or the first account if none is marked active.
6. Removes repository bindings whose account no longer exists.
7. Persists and publishes public state.

### 9.3 Account switching

Switching the active Relay account calls `gh auth switch`. It is not merely a
UI selection. This keeps GitHub CLI's active account and Relay's active
account aligned.

### 9.4 Commit email

The account email is a commit identity setting, not an authentication setting.
Changing it does not change the GitHub account and does not rewrite old commits.

An empty submitted value becomes:

```text
<numeric-github-id>+<handle>@users.noreply.github.com
```

### 9.5 Repository account routing

When a Git or clone operation needs an account, resolve it as follows:

```text
repositoryAccounts[repositoryPath] ?? activeAccountId
```

The clone dialog uses the globally active account because a cloned repository
does not yet have a path binding.

### 9.6 SSH identities

GitHub accounts and SSH identities are deliberately separate models. An account
is an OAuth identity held by the GitHub CLI; an SSH profile is only a hint about
which key to offer a host. Do not model an SSH host as a GitHub account. A
profile can be selected for any matching SSH host, including github.com, and
needs no GitHub sign-in (see "Native usability" in section 18).

With no profile bound to a repository, Relay sets nothing, so the user's SSH
agent and `~/.ssh/config` behave exactly as they do for `git` on the command
line. That is the default and covers most setups.

When a profile is bound, `SshService` builds a `GIT_SSH_COMMAND` for that
single Git invocation:

```text
'ssh' -i '<identityFile>' -o 'IdentitiesOnly=yes' [-p <port>]
```

`IdentitiesOnly` is what lets several identities share one host; without it
OpenSSH offers every agent key and the server closes the connection after too
many attempts. Paths are POSIX single-quoted, including on Windows, because Git
hands `GIT_SSH_COMMAND` to a shell.

Rules that keep the credential shapes from crossing:

- `GIT_SSH_COMMAND` is attached only to an SSH transport URL, and the profile
  host is checked against the actual URL (including the origin push URL);
  a mismatch is refused before SSH starts.
- An OAuth token is requested only for an HTTPS github.com remote, and the
  credential helper answers only HTTPS github.com `get` requests.

`SshService::testConnection()` runs an authentication-only `ssh -T` with
`BatchMode=yes`, so it can never hang on an invisible passphrase prompt, and
`describeResult()`
turns the outcome into one actionable sentence that names the host without
echoing key paths back to the user.

### 9.7 Token handling

`GitHubAuth::accountToken()` asks GitHub CLI for one named account token on a
controller worker thread. The token is never stored in a member, model, or
signal.

For an HTTPS `github.com` clone, fetch, or push, the Git service:

1. Clears the ordinary Git credential helper for that command.
2. Installs a command-scoped helper that prints username and password.
3. Passes the token through `RELAY_GIT_TOKEN` in the child environment.
4. Sets `GIT_TERMINAL_PROMPT=0` to avoid hanging on an invisible prompt.

Never log the child environment or the constructed credential response. Never
embed a token in the remote URL, Git configuration, command history, widget
or model state, error text, or release metadata.

SSH URLs do not use a GitHub OAuth token. They use the user's existing SSH
configuration and keys.

## 10. GitHub REST API Usage

`GitHubApi` uses GitHub REST API version `2022-11-28` and the
`application/vnd.github+json` media type.

### Profile request

New accounts are enriched from:

```text
GET https://api.github.com/user
```

### Repository browser

The clone browser uses:

```text
GET /user/repos
  ?visibility=all
  &affiliation=owner,collaborator,organization_member
  &sort=updated
  &direction=desc
  &per_page=100
```

The implementation follows `Link` headers until no `rel="next"` link remains.
It keeps only repositories the account can push to that are not archived, and
the dialog says how many were hidden. Commit-email choices come from
`GET /user/emails` when the `user:email` scope is available.

### Pull requests

**Repository → Create or open pull request** applies to a `github.com` origin
(`GitHubApi::repositoryFromRemote()` anchors the whole host and validates
owner/name). The branch must exist as `refs/remotes/origin/<branch>`. With
unpushed commits the window offers **Push first** (then continues only if the
push left `ahead == 0`) or opening anyway. With a GitHub account the worker
calls `GET /repos/{owner}/{repo}/pulls?state=open&head={owner}:{branch}`;
an existing PR's `html_url` is opened only if it is exactly
`https://github.com/{owner}/{repo}/pull/<number>`. Otherwise, and without an
account, `https://github.com/{owner}/{repo}/compare/<branch>?expand=1` opens.
The URL reaches the window through `pullRequestReady`; tokens never do. A
fork's pull request to its parent is chosen on GitHub's page.

API requests run in controller workers. Never hand a token to a widget so it
can call GitHub itself. Gitea/Forgejo/GitLab discovery is described in section
18 ("Native multi-host and branch work").

## 11. Bundled Runtime Resolution

The large `runtime/` directory is intentionally absent from Git history but is
present in a release-building workspace.

Expected development/runtime layout:

```text
runtime/
  git/
    mac-arm64/
      bin/git
      libexec/git-core/
      share/git-core/templates/
      etc/gitconfig
    win-x64/
      cmd/git.exe
      mingw64/bin/
      mingw64/libexec/git-core/
      mingw64/share/git-core/templates/
      mingw64/etc/ssl/certs/ca-bundle.crt
  gh/
    mac-arm64/gh
    win-x64/gh.exe
```

Packaged locations are under `process.resourcesPath`:

```text
Resources/git/...
Resources/gh/gh              # macOS
resources/git/...
resources/gh/gh.exe          # Windows
```

Git selection order:

1. Packaged Git under `process.resourcesPath` if present.
2. Development runtime under `runtime/git/<platform>` if present.
3. `git` on `PATH`.

GitHub CLI selection order:

1. Packaged `gh` under `process.resourcesPath` if present.
2. Development runtime under `runtime/gh/<platform>` if present.
3. `gh` or `gh.exe` on `PATH`.

Bundled Git is relocatable only when its helper paths are set correctly. Preserve
the construction of:

- `GIT_EXEC_PATH`
- `GIT_TEMPLATE_DIR`
- `GIT_CONFIG_SYSTEM`
- `PATH`
- `GIT_SSL_CAINFO` on Windows

Missing `GIT_EXEC_PATH` can produce a misleading failure such as
`git: 'remote-https' is not a git command` even when the helper binary exists.

## 12. Git Service Semantics

All Git calls use `execFile`, not a shell. Continue passing arguments as an
array. Put `--` before user-controlled paths or remotes whenever the Git command
supports it.

### 12.1 Repository read

`readRepository()` returns:

- Canonical top-level path from `rev-parse --show-toplevel`
- Current branch or `detached HEAD`
- Porcelain v1 status (NUL-delimited), including untracked files
- `origin` URL if present
- Local branches, remote-tracking branches, tags, stashes, and any operation
  in progress (merge, rebase, cherry-pick, revert)
- Up to 30 history entries, used only as a HEAD signal for the History tab
- Parsed changed files and approximate line counts
- Ahead/behind values relative to the upstream or matching remote branch
- Whether a configured upstream exists
- `latestCommit`, the committer date of `HEAD`, or `null` with no commits

GitHub owner/name is inferred from HTTPS or SSH GitHub remotes. For non-GitHub
or missing remotes, owner falls back to the parent folder and name to the root
folder.

### 12.2 Status parsing

The file model reduces Git status to added, modified, or deleted. It does not preserve every two-character Git
status combination.

Untracked text files smaller than 2 MiB get an approximate added-line count by
reading the file. Binary and large files retain zero cosmetic line counts.

### 12.3 Diffs

- Tracked files use `git diff HEAD --no-ext-diff --unified=3`.
- A staged-diff fallback is used when the first command fails.
- Untracked text files are represented as a synthetic all-added unified diff.
- Untracked binary files return `Binary file — preview unavailable`.
- `DiffModel` parses hunk headers and tracks old/new line numbers; `DiffView`
  is a virtualized table.

### 12.4 History reads

The History tab does not use the 30 entries in the repository payload. It calls
three narrow methods instead, so opening a repository never pays for its whole
history.

- `readHistoryPage()` returns one batch. Paging is anchored to an explicit
  commit resolved on the first page, not to `HEAD`, so a `HEAD` that moves
  while the user scrolls cannot make later pages skip or repeat commits. It
  reports `endOfHistory` rather than imposing a cap. Git is asked for a
  window of 5,000 commits (topological order: the graph and several tips) or
  2,000 (date order: the plain list of one branch), and later pages of the
  same snapshot are served from that window in memory without starting Git.
  Without a commit-graph file, `--topo-order` walks the whole history before
  printing anything, so one walk per window instead of per page is what makes
  long histories scroll. A first page (no snapshot) always reads fresh.
- `searchHistory()` searches the same scope as the history view by message
  (including the body), author name or email, and hash prefix,
  case-insensitively and literally. Git ANDs `--grep` with `--author`, so they
  are separate walks merged newest first; at most 200 results. The UI filters
  loaded commits as the user types and runs this when typing pauses and more
  history exists.
- `readCommitDetail()` returns full and short hashes, subject and body,
  separate author and committer identities, parents, decorations, and the
  changed files with their add/modify/delete state and line counts. A merge is
  compared against its **first parent**; a root commit has no parent and is
  compared against the empty tree, so its files read as added. `isSigned`
  reports a signature header without verifying it.
- `readCommitFileDiff()` returns one file's diff against that same parent.

Commit hashes and file paths are untrusted even when they come from Relay's
own models. The shape is checked and `git cat-file -e <hash>^{commit}` runs against the open repository, so a real hash
from a different clone is refused. File paths always travel after `--`.

### 12.5 Commit

Commit behavior is intentionally file-selective:

1. Validate at least one file and a non-empty summary.
2. `git add -- <selected files>`.
3. Run `git commit --only` with the same selected files.
4. Supply account name and email through both `-c` values and author/committer
   environment variables.
5. Add summary and optional description as separate `-m` paragraphs.

Do not silently commit every working-tree change.

Co-authors come from the commit box's field (`Name <email>`, comma- or
line-separated; **Recent** offers up to 15 authors from the repository's
latest history). `GitService::parseCoAuthors()` rejects anything else,
including control characters and `:` in names, drops duplicates and the
committer, and `commitFiles()` parses again before adding each one as
`--trailer=Co-authored-by: Name <email>`.

An account with a `signingKey` (a path to an SSH key, account menu → Commit
signing) signs its commits, including merges, reverts, cherry-picks, rebases
and amends. `GitService::addSigningConfiguration()` appends `gpg.format=ssh`,
`user.signingkey` and `commit.gpgsign=true` through `GIT_CONFIG_*` after any
entries already in the inherited environment. Without a key, Git's own
configuration decides.

### 12.6 Fetch, pull and push

- Fetch runs `git fetch origin --prune`.
- Push runs `git push --set-upstream origin HEAD`.
- Force push (Repository menu and the sync menu, **Force push origin…**) is
  enabled when `refs/remotes/origin/<branch>` exists. The confirmation names
  the last fetched origin tip; that full hash travels to the service, which
  refuses if the tracking ref has moved since and then runs
  `git push --set-upstream --force-with-lease=refs/heads/<b>:<hash> origin
  HEAD:refs/heads/<b>`. Origin changed after the fetch is a "stale info"
  rejection with an actionable message. Never use a bare `--force` or a lease
  without an explicit expected hash: a background fetch would defeat it.
- **Repository → Push tag to origin… / Delete tag on origin…** push one
  local tag as `refs/tags/<t>:refs/tags/<t>` without force (a different tag
  of that name on origin is reported, not replaced), or list origin's tags
  with `ls-remote --tags --refs` and delete the chosen one with
  `--force-with-lease=refs/tags/<t>:<listed id>`. Local tags are never
  touched. All origin network operations share `GitService::originTransport()`
  and the controller's worker-only `originCredentials()`.
- **Repository → Open in <editor> / Open in Terminal / Show in Finder
  (Explorer)** (`Ctrl+Shift+A`, ``Ctrl+` ``, `Ctrl+Shift+F`) go through
  `RelayController::openRepositoryIn()` to `ExternalApps`
  (`external_apps.cpp`) on a worker. Programs start detached with the
  repository folder as a single argument and working directory, never through
  a shell. macOS uses `/usr/bin/open -a <app|Terminal>` and `open <folder>`;
  Windows uses the editor executable, Windows Terminal (`wt.exe -d`) or
  `%ComSpec% /K` in the folder, and `explorer.exe`. Editors are detected in
  standard install locations; `preferences.editorId` (detected id, `custom`,
  or empty for the first detected) and `preferences.editorPath` (custom
  absolute `.app` or executable) persist the choice. Widgets receive only
  editor ids and names (`editorsDetected`); detection runs at startup with
  the runtime check and again when Settings opens.
- **Repository → Compare branches…** opens the non-modal `CompareDialog`
  (`compare_dialog.cpp`). It only emits requests; `compareBranches()` accepts
  `refs/heads/` and `refs/remotes/` refs, lists up to 500 commits on each side
  and the files changed from the merge base (or the base tip for unrelated
  histories) to the compared tip, like a pull request. File diffs come from
  `readComparisonFileDiff()` with validated hashes. Results carry their refs
  and own request generations, so stale ones are dropped. The dialog follows
  repository refreshes, closes on a repository switch, preselects the
  History scope's branch, and offers **Merge X into current** (the existing
  merge action) when the base is the current branch.
- **Repository → Squash and reorder commits…** reads commits in
  `HEAD --not --remotes` (newest first, at most 100, no merges) through
  `readUnpublishedCommits()`. `CommitRewriteDialog` reorders them (drag or
  Move up/down), squashes a checked row into the row below, and edits the
  group's message (default: the group's messages oldest first).
  `rewriteUnpublishedCommits()` requires a clean worktree and a plan naming
  exactly the unpublished commits once, then writes a `pick`/`fixup` todo
  plus `exec git commit --amend --only -F <file>` for new messages under
  `git-path relay-rewrite/`, and runs `git rebase --interactive --empty=keep`
  with `GIT_SEQUENCE_EDITOR=cp '<todo>'` (POSIX-quoted; Git for Windows' sh
  has `cp`), `rebase.updateRefs=false` and `rebase.autoSquash=false`, onto
  the oldest commit's parent or `--root`. A conflict leaves an ordinary
  rebase for the conflict dialog; `relay-rewrite/` is removed when the rebase
  ends, including through continue/skip/abort. Identity and signing follow
  other commit-producing actions.
- **Repository → Delete branch on origin…** lists fetched `origin/*` branches
  (the History scope's origin branch first), confirms with the tip's full
  hash for recovery, and runs `git push --force-with-lease=refs/heads/<b>:<hash>
  origin :refs/heads/<b>` with the same credential/SSH routing as push. The
  service validates the name, requires the tracking ref to equal the
  confirmed hash and refuses origin's default branch (`origin/HEAD`). Local
  branches are never deleted by it. Other remotes are not offered.
- Both accept an optional SSH command, applied only to an SSH remote.
- Fetch/push require an `origin` remote.
- Push requires a named local branch.
- The main sync button suggests fetch or push from the last known state; its
  menu offers Fetch, Pull and Push directly. Pull fetches, then fast-forwards
  the origin upstream. When both sides have commits it changes nothing and
  emits `pullDiverged`; the window asks whether to merge or rebase, and the
  answer runs the existing merge action, or plans the rebase (identity check,
  published-commit warning, conflict dialog) without asking a second time.
- **Repository → Rebase current branch…** first calls
  `RelayController::planRebase()` (`GitService::planRebase()`: commits in
  `HEAD --not <onto>`, and how many of them a remote branch contains). With no
  published commits it asks once and runs `rebaseBranch`, which still refuses
  published commits. Otherwise a warning offers Merge instead or
  **Rebase and rewrite**, which runs `rebasePublished`; when that rebase ends
  (also after conflicts) and the branch has diverged from origin, the window
  offers the lease-protected force push once.

### 12.7 Clone

Clone runs:

```text
git clone --progress -- <remoteUrl> <destinationPath>
```

The controller and service validate:

- URL starts with `http://`, `https://`, `ssh://`, or `git@`.
- Parent folder is supplied and exists.
- Repository folder name is one basename, not `.` or `..`.
- Destination does not already exist.

After a successful clone, Relay fully reads and remembers the new repository.

### 12.8 Branch switching

The current-branch picker lists local branches and, as `Remote · <remote>/<branch>`,
remote-tracking branches other than symbolic `HEAD` refs.

- A local branch: `GitService::switchBranch()` verifies `refs/heads/<name>` and
  runs `git switch --no-guess <name>`.
- A remote branch: `RepositoryAction::checkoutRemote` runs
  `git switch --create <name> --track refs/remotes/<remote>/<name>`, or, when
  a local `<name>` exists, `git switch --no-guess <name>` without changing its
  upstream. The current-branch picker leaves out remote branches with a local
  namesake; the history scope still lists them.

Remote branches are what the last fetch recorded. **Fetch all origin branches**
(History) and **Fetch all branches from origin** (last current-branch picker
item, shown when `origin` exists) fetch the full heads refspec, also for
single-branch clones, without switching branches. The picker item's data is
`relay:fetch-origin-branches`, never a ref. Names starting with
`-` are rejected, and the picker shows the actual branch until a switch
succeeds.

## 13. Repository Discovery Semantics

Folder scanning is breadth-first and filesystem-based; it does not run one Git
command per directory just to detect repositories.

A directory is considered a repository when it contains:

- A `.git` directory, or
- A `.git` file whose contents begin with `gitdir:`; this supports linked
  worktrees.

Once a repository root is found, the scanner does not descend into it. This
avoids scanning internal Git data and nested submodule working directories from
the same root traversal.

The controller reads discovered repositories in batches of 10. Individual
unreadable or invalid repositories are ignored rather than failing the complete
scan. The response distinguishes:

- `found`: filesystem markers found
- `readable`: markers that produced a valid repository summary
- `added`: readable repositories not already remembered

The scanner must remain non-destructive.

## 14. Menus

`MainWindow::buildMenus()` builds one `QMenuBar`. On macOS Qt places it in the
system menu bar and moves role-tagged actions (Settings, Quit) into the
application menu; on Windows it is the window's own menu bar. Workflow actions
add further Repository menu items from `workflow_ui.cpp`.

- **File:** Add Local Repository… (`QKeySequence::Open`), Clone Repository…
  (`Ctrl+Shift+O`, Cmd on macOS), Scan Folder for Repositories…, Remove Current
  Repository from Relay, Quit Relay (`QuitRole`).
- **Edit:** Undo, Redo, Cut, Copy, Paste, Select All (targeting the focused
  editor or diff), Settings… (`PreferencesRole`).
- **Repository:** New branch… (`Ctrl+Shift+N`), Pull origin, and the repository
  actions.
- **View:** Reload, Force Reload, Actual Size, Toggle Full Screen.
- **Window:** Minimize, Close.
- **Help:** Check Git and GitHub CLI.

When adding a menu item, add it once, route it to a controller slot, and check
labels and shortcuts on both platforms.

## 15. Visual and Interaction System

The UI resembles GitHub Desktop structurally without copying its source or
branding.

```text
menu bar (system on macOS, in-window on Windows)
repository action row: repository | branch picker | sync (fetch/pull/push menu) ... account/identity
workspace (splitters):
  repository sidebar (filter, ordering control, repository list)
  main panel:
    Changes tab: file list + commit box | diff or image preview
    History tab: branch scope, List/Graph, search | commit list | details, files, diff
status bar: identity and transport | repository settings
```

- Colors and the style sheet come from `theme.cpp` and the active palette
  (section 5.3). Do not hard-code colors in widgets. Do not add rounded
  corners (section 5.3).
- Icons are flat SVG resources (chevrons from Lucide). Do not use emoji, icon
  fonts, gradients, or highlight effects.
- Avatars are GitHub profile pictures from `AvatarCache`, with flat initials as
  the fallback.
- Every icon-only button has an accessible name.
- The account menu is a `QMenu`, which closes on an outside click.
- Long-running operations show busy state; failures appear as notices, and
  conflict errors stay inside their modal dialog.

## 16. Development Commands

macOS (Apple Silicon, `brew install llvm@20`):

```bash
cmake --preset macos-debug -DCMAKE_PREFIX_PATH=/path/to/Qt/6.11.1/macos
cmake --build --preset macos-debug --parallel
ctest --preset macos-debug
open build-native/macos-debug/native/Relay.app
```

Windows x64, from an x64 Visual Studio developer environment with LLVM 20 and
the Qt MSVC 2022 x64 build:

```bat
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel
ctest --preset windows-debug
```

Local source installers: `native/tools/install-macos.sh` and
`native/tools/install-windows.ps1` (see section 6 and README).

Homebrew Qt is acceptable for development but not the final packaging baseline
because its frameworks carry Homebrew-specific transitive library paths.
Release/measurement builds must use the pinned official Qt distribution and
reject staged dependencies under `/opt/homebrew` or `/usr/local`.

After editing `build/icon.svg` or `build/icon-small.svg`, regenerate the icons:

```bash
cmake --build --preset <preset> --target icons
```

`native/tools/icon_generator.cpp` renders 48px and below from the small
master, writes every `.icns` slot (including @2x) at its true pixel size, and
is byte-stable. QtSvg does not implement `feDropShadow`, so `icon.svg` spells
the shadow out as the primitives it is defined by; keep it that way.

Bundled runtimes for installers or a development `runtime/`:

```bash
python native/tools/fetch_runtimes.py --platform win-x64 --output runtime
```

Versions and SHA-256 digests are pinned in `native/packaging/runtimes.json`.

## 17. Testing and Verification Strategy

Match verification effort to risk.

- Every change: configure, build, and run `ctest` with the applicable preset.
  `app_smoke` starts the app offscreen with `--smoke-test` and a disposable
  profile.
- Service changes: extend the matching `native/tests/tst_*.cpp` using
  temporary Git fixtures.
- UI changes: extend `tst_main_window.cpp` or `tst_dialogs.cpp`, and launch the
  built app to exercise the click sequence.
- Installer changes: `test_install_windows.py`, `test-preview-windows.ps1`,
  and the CI installer steps.
- Pushing a pull request that touches `native/**`, `CMakeLists.txt`,
  `CMakePresets.json` or the workflow runs `.github/workflows/native.yml` on
  macOS arm64 and Windows x64 with Qt 6.11.1. That is the authoritative check;
  Linux builds only supplement it. CI runs suites with `ctest --parallel 4`
  and disables Defender real-time scanning on the disposable Windows runner,
  so every test must keep using its own temporary directories and profiles.
  `git_workflows` runs as `git_workflows_1` and `git_workflows_2`:
  `tests/test_shard.hpp` gives each `RELAY_TEST_SHARD=i/n` every n-th test
  function in declaration order, so new functions are never left out. Use it
  for another suite that becomes the longest one on Windows. Steps that start
  Git wait up to `gitTimeoutMs` (30 s) in the controller and main-window
  suites: the v0.5.4 tag build failed a 5-second wait under parallel load.

Git fixtures should cover, as relevant: clean, modified, untracked, binary,
no commits, with and without `origin` and upstream, local and remote-only
branches, conflicts, a linked worktree, several repositories in one tree, and
clone/fetch/pull/push over a local transport. Never point destructive tests at
a user's real repository.

Authentication tests must not print tokens. Prefer account counts and sanitized
shapes, use a disposable profile when login state is not needed, and use the
injected memory vault for forge credentials. Real OAuth, private repositories
and OS credential stores remain manual checks on installed builds.

Packaging verification:

- macOS: the app and its frameworks are arm64; `codesign --verify` passes on
  the ad-hoc signature; the DMG verifies and its copied app launches.
- Windows: `Relay.exe` is PE x86-64 with a valid manifest; the MSVC runtime and
  Qt plugins are deployed; Setup upgrades in place and preserves unrelated and
  Electron folders.
- Both: the installed app starts with no repository open and the Git/gh check
  passes with external tools.

## 18. Packaging and Release Model

There are two packaging paths:

- **Preview installers (current releases).** CI builds the source install,
  then packages an Inno Setup `Relay-Native-Setup-<version>-x64.exe` and an
  ad-hoc signed `Relay-Native-<version>-arm64.dmg`. They require external Git
  and gh. Details below.
- **Strict release packaging.** `RELAY_ENABLE_PACKAGING=ON` with CPack bundles
  Git and gh from `runtime/` and verifies the stage. See
  `native/packaging/README.md`. Not used for a release yet.

### Native usability included in refreshed 0.5.2

The toolbar synchronization button has a split menu exposing Fetch, Pull and
Push independently of the cached ahead/behind counts. Pull still requires an
origin upstream and fast-forwards only. Changes/history horizontal splitters
have wider visible handles; history details/files and the diff now use a
vertical splitter instead of a fixed-height changed-file list.
The account popover lists repository SSH profiles alongside GitHub account
management. Matching SSH hosts, including GitHub.com, can select a saved profile or the
normal SSH agent without a GitHub account. The selected transport is displayed
in the toolbar/status bar; commit identity still follows existing account/Git
configuration precedence. Native clone/fetch/pull/push apply the selected SSH
profile to GitHub SSH URLs as explicitly requested by the owner. HTTPS keeps
OAuth routing. Validate the profile host against the actual transport URL
(including origin push URL); refuse mismatches before launching SSH. No tokens
or key material enter widgets. UI integration tests cover remote changes pulled before a fetch,
resizing a populated diff, persisted SSH selection and host mismatch rejection.
The Windows main-window suite has a 300-second CTest limit because its real
Git fixtures exceed the default 60 seconds on CI (180 seconds stopped being
enough with the 0.5.4 cases in run 37954523555); individual waits stay bounded.

### Native multi-host and branch work (2026-09-25)

The native current-branch picker includes local and remote-tracking branches.
History has a separate scope selector (current/all/specific local or remote).
Reading another branch never checks it out. Explicit checkout and creation
from a selected branch are separate actions. History paging snapshots commit
tips and applies request generations across scope changes. The explicit
Fetch all origin branches action fetches the complete heads refspec, even for
single-branch clones, without changing Git configuration or pruning refs.
Other remotes' already-fetched refs remain visible.

Multiple selected history commits use one Git cherry-pick sequencer operation
in displayed oldest-to-newest order, capped at 200; validate every object and
duplicates before mutation. Continue/skip/abort preserve the full sequence.
Merge picks use first parent. On 2026-10-09 the owner asked for every
feature, adding lease-protected force push and origin branch deletion
(section 12.6). Existing merge/rebase/revert/stash/conflict actions remain.

New forge_service/forge_controller/forge_dialog files implement API discovery
for Gitea/Forgejo and GitLab, including self-hosted HTTPS/subpath servers.
Public AppState.forgeAccounts is separate from GitHub OAuth accounts and
commit identity. Paginate access/member repositories, retaining private,
read-only and archived entries. These API credentials are not Git transport
credentials; cloning uses the chosen SSH profile. Browser token-settings links
support manual token creation; direct OAuth requires a registered application
and is not claimed. Other Git services still work through generic Git/SSH.

credential_store.cpp uses native Security.framework or Advapi32 APIs, only
from controller workers; Linux has no plaintext fallback. Inject a memory
vault only in tests. User-entered tokens leave the password input immediately;
widgets never receive them back. Vault references bind to the deterministic
provider/server/user identity. Reconnection writes a new unique key, publishes
metadata only after saving, and retains the previous credential on save failure.
An uncommitted result owns cleanup even if its controller closes. Persisted
forgeCredentialCleanup references permit retry after vault removal errors.
Drop stale API successes and failures after account selection/dialog closure.
Requests are HTTPS, bounded, redirect-free; never surface raw remote errors.

Tests cover paginated discovery/security, native OS vault roundtrip on target
platforms, connect/browse/restart/disconnect UI, failed metadata/vault writes,
controller shutdown, stale requests, branch browsing without checkout and
multi-commit conflict continuation. Private real-server authentication remains
an installed-platform manual check; do not claim fixtures prove it.
Windows Git service/workflow suite budgets are 180/300 seconds (per workflow shard): run
36154921968 passed the new cases but exhausted the former total suite limits
near the final fixtures. Individual process limits remain unchanged. Gitea PAT
requests use Authorization: token; GitLab uses Bearer. Discovery has a 64 MiB
aggregate payload bound in addition to per-request size and time limits.

### Native preview installers (0.5.2 onward)

CI packages its validated source-build payloads with Inno Setup 6 on Windows
(`native/packaging/preview/windows.iss`) and a standard DMG on macOS
(`native/tools/build-preview-dmg.sh`). These remain unsigned previews using
external Git/gh, separate from strict bundled-runtime release packaging.
The stable Inno AppId is `dev.relay.native.preview`, distinct from the old Electron installer.
Keep it stable across native versions so upgrades reuse the per-user directory
and uninstall registration. The wizard always offers a directory page and an
optional desktop shortcut. It adopts 0.5.1 ZIPs through their installation.json,
refuses unrelated/Electron directories and junctions, and refuses locked app
executables. Never delete profiles or repositories from installer scripts.
Source installs refuse to replace an Inno-managed app so its uninstall record
cannot be orphaned. Windows CI tests a custom-directory upgrade, both shortcut
choices, real 0.5.1 ZIP adoption, locked-file refusal and Electron preservation.
macOS uses the same Relay Native.app filename; Finder replaces it in its existing
location without an uninstall. CI verifies the DMG and launches its copied app.

### 18.1 Release checklist

1. Set the version in `CMakeLists.txt` (`project(RelayNative VERSION ...)`).
2. Update the version mentioned in README's Download section and section 3.
3. Merge to `main` through a pull request and wait for the native CI run on
   both platforms to pass.
4. Push the tag: `git tag v<version> && git push origin v<version>`. The
   workflow builds and tests both platforms again, checks the tag matches
   `CMakeLists.txt`, and publishes a prerelease with the DMG, the Windows
   setup and `SHA256SUMS.txt`.
5. Install each installer on a real machine and smoke-test: startup, Git/gh
   check, sign-in, a clone, a commit, fetch/pull/push.
6. Edit the generated release notes if needed.

Installers bundle the GitHub CLI on both platforms and Git for Windows on
Windows (`native/tools/fetch_runtimes.py`). macOS has no official
relocatable Git build; Git comes from the Xcode Command Line Tools or
Homebrew there.

Builds are not Developer ID signed, notarized, or Authenticode signed. Ad-hoc
signing stops Apple Silicon from calling the app damaged but is not
distribution signing. Do not claim otherwise.

## 19. Common Change Recipes

### Add a privileged operation

1. Implement the work in a service, synchronous and headless.
2. Validate every external value in the service.
3. Add a `RelayController` slot that runs it through `runAsync()`, with a
   generation or freshness check if the result can go stale, and the
   repository/account mutation lock if it mutates.
4. Return only sanitized domain values through a signal.
5. Connect the view.
6. Add a QtTest case.

### Add a Git operation

1. Implement it in `GitService` or `git_workflows.cpp` with an argument list,
   `--` separators, and literal pathspecs for user paths.
2. Decide whether GitHub HTTPS credentials or an SSH command apply.
3. Set `GIT_TERMINAL_PROMPT=0` for network operations.
4. Return a refreshed `Repository` after a mutation.
5. Test no-remote, no-branch, conflict, failure, and success cases.

### Add account or preference metadata

1. Add a safe default and preserve stored values on synchronization.
2. Preserve unknown fields on write.
3. Clean dependent bindings when an account is removed.
4. Never store secrets; forge API tokens go to the OS credential store.

## 20. Security Checklist

- Does any token reach a widget, model, signal, log, or error message?
- Could an inherited `GH_TOKEN` select a different account?
- Could a path escape the intended repository or clone parent?
- Are user-controlled Git arguments separated from options?
- Could a command prompt invisibly and hang?
- Could an external URL use a non-HTTPS scheme?
- Could a GitHub token reach a host that is not github.com, or a forge token
  reach a server other than the one it was created for?
- Could a private key path, key contents, or a passphrase reach the store, the
  UI, a log, or an error message?
- Could a commit hash or file path address an object outside the open
  repository?
- Could a discard, stash, rebase or undo lose user data without a recovery
  path?
- Does repository removal touch disk data?
- Does a commit include installers, runtime binaries, local app data,
  credentials, or `.env` files?

## 21. Error Handling Conventions

- Service errors are one actionable sentence where possible.
- Git stderr loses its leading `fatal:`.
- GitHub CLI errors use the last non-empty cleaned output line.
- Authentication expiry tells the user to sign in again.
- A canceled dialog is not an error.
- Bulk scanning reports counts instead of failing everything.
- Busy state clears when an operation finishes, whether it failed or not.
- Never surface raw remote API error bodies.

Do not swallow errors that make an operation look successful.

## 22. Known Limitations

- GitHub OAuth is GitHub.com only. Gitea/Forgejo/GitLab accounts are for API
  discovery; their Git transport is SSH or existing Git credentials.
- Force push and remote branch deletion exist only in the lease-protected,
  origin-only forms described in section 12.6.
- Published commits can be rebased only after a warning, and cannot be undone
  or amended here.
- History beyond a cached window still uses `--skip`, so very deep history
  costs one Git walk per 2,000 or 5,000 commits. History search returns at most
  200 matches. Merges compare against the first parent.
- Ahead/behind is approximate when upstream information is incomplete.
- Commit signing uses SSH keys per account; GPG signing is only whatever the
  user's Git configuration does. Signatures are shown, not verified.
- SSH identities cover the transport only. Relay does not create keys, edit
  `~/.ssh/config`, or handle passphrases.
- Scanning and recents are capped at 5,000 repositories.
- macOS installers bundle gh but not Git. The strict CPack pipeline is not
  used for releases yet.
- Real OAuth, private repositories, forge servers and OS credential stores are
  not covered by automated tests.
- Builds are not distribution-signed. There is no auto-update, crash reporting,
  or telemetry.

Treat this list as context, not authorization to expand scope.

## 23. Agent Working Agreement

1. Read `AGENTS.md`, `README.md`, and the relevant `native/` sources.
2. Inspect `git status` before editing. Preserve unrelated user changes.
3. Keep changes focused and preserve the invariants.
4. Do not edit generated output when a source change can regenerate it.
5. Do not commit `runtime/`, `outputs/`, `work/`, `build-native/`, or
   credentials.
6. Do not perform remote writes without explicit authorization.
7. Test at the narrowest level first, then at the real integration boundary.
8. Document new architecture, persistence, security, build, or release
   behavior here.
9. Report unsigned-build limitations honestly.
10. Never describe Relay as a demo. It is intended to be a working application.

## 24. Definition of Done

- Requested behavior works through the actual user-facing path.
- No invariant regressed.
- Credentials stay out of the UI, logs, disk metadata, and Git.
- The project builds with the applicable preset and `ctest` passes, including
  `app_smoke`; for anything merged, native CI is green on both platforms.
- Relevant Git or provider integration was exercised safely.
- Both macOS and Windows implications were considered.
- Visual behavior was inspected if UI changed.
- `AGENTS.md` and `README.md` remain accurate.
- Only requested local or remote state was changed.
