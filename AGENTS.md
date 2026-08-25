# Relay Desktop: Coding Agent Guide

This document is the authoritative engineering guide for coding agents working
on Relay. Read it before changing code, running release commands, modifying Git
or GitHub authentication, or publishing anything.

It describes the repository as implemented at Relay `0.4.0`. When this document
and the code disagree, treat the code as the immediate source of truth and
update this document in the same change.

## 1. Product Definition

Relay is a small GitHub Desktop-style Electron client whose distinguishing
feature is first-class support for multiple GitHub accounts.

The supported production targets are deliberately narrow:

- macOS on Apple Silicon (`arm64`)
- Windows 64-bit (`x64`)
- GitHub.com, not arbitrary GitHub Enterprise hosts

Relay provides these working flows:

- Connect multiple GitHub accounts using the official GitHub CLI browser/device
  OAuth flow.
- Switch the active GitHub account without entering a personal access token.
- Assign a specific GitHub account to a specific local repository, or let that
  repository follow the globally active account.
- Set a distinct commit email for each connected account.
- Open an existing local Git repository.
- Recursively scan a parent folder and remember all repositories found below it.
- Remove a repository shortcut from Relay without deleting anything on disk.
- Browse repositories available to the active GitHub account, including private,
  collaborator, and organization repositories.
- Clone a repository selected from that list, or clone an arbitrary HTTPS or SSH
  URL.
- View working-tree changes and textual diffs.
- Browse the full branch history, loaded progressively, with per-commit
  metadata, changed files, and per-file diffs.
- Sort the repository sidebar manually, by age, by name, or by latest commit.
- Select an SSH identity for a repository on a Git host other than GitHub.com.
- Select changed files and create a commit using the selected account identity.
- Fetch from and push to `origin` using the selected GitHub account.
- Switch between existing local branches.

Relay is not currently a complete GitHub Desktop replacement. See
"Known Limitations" before promising or assuming behavior.

## 2. Non-Negotiable Product Invariants

Preserve these behaviors unless the user explicitly requests a product change:

1. **Start with no repository open.** Recent repositories may remain in the
   sidebar, but application startup must not automatically reopen one.
2. **Never ask for a PAT.** Authentication is browser-based GitHub CLI OAuth.
3. **Never persist an OAuth token in `relay-data.json`, React state, logs, or Git
   configuration.** Tokens may exist only transiently in the main process and a
   child process environment.
4. **Removing a repository from Relay never deletes, moves, cleans, resets, or
   otherwise modifies repository files.** It removes only Relay metadata.
5. **Repository identity follows this precedence:** an explicit per-repository
   account binding first, otherwise the globally active account.
6. **Account commit email is independent of the GitHub login email.** A blank
   value resets to GitHub's numeric-ID noreply address.
7. **The account switch popover closes when the user clicks outside it.**
8. **Use flat, code-native SVG icons.** Do not introduce emoji as UI icons,
   glossy/illuminated icons, or icon gradients.
9. **Keep the signed-in status visually quiet.** Do not restore the removed
   green status dot or an "All systems operational" message.
10. **macOS and Windows must have equivalent File-menu functionality.** Windows
    uses the normal visible application menu; macOS uses the system menu bar.
11. **The renderer never receives a GitHub token and never has direct Node.js
    access.** All privileged operations cross the preload bridge.
12. **External navigation is HTTPS-only and opens in the system browser.**

These invariants are partly covered by tests, but many remain behavioral and
must also be checked manually.

## 3. Repository Identity and Publication

- Public repository: <https://github.com/ka-capek/relay-desktop>
- Default branch: `main`
- Current release line: `v0.4.0`
- Release page: <https://github.com/ka-capek/relay-desktop/releases>
- App ID: `dev.relay.gitclient`
- Electron product name: `Relay`
- npm package name: `relay-desktop`

Do not push, publish a release, change repository visibility, or modify GitHub
settings unless the user explicitly authorizes the remote write.

## 4. Technology Stack

The desktop product uses:

- Electron 44
- React 19
- TypeScript for the renderer and build configuration
- CommonJS for Electron main-process modules
- Vite 8 for the desktop renderer bundle
- electron-builder 26 for DMG and NSIS packaging
- Git as a child process for all repository operations
- Official GitHub CLI (`gh`) for multi-account OAuth and credential lookup
- OpenSSH, through the user's agent and configuration, for non-GitHub remotes
- GitHub REST API for profile data and repository listing
- Plain CSS for the complete UI and responsive desktop layout

The repository also contains Vinext, Cloudflare, Drizzle, and worker scaffolding.
That path can server-render the React UI and is used by the current test command,
but it is not the installed desktop runtime. Do not confuse `npm run dev` with
running the Electron application.

## 5. Architectural Overview

```text
React renderer (app/page.tsx)
        |
        | window.relayDesktop (typed in renderer)
        v
context-isolated preload bridge (electron/preload.cjs)
        |
        | ipcRenderer.invoke / event listeners
        v
Electron main process (electron/main.cjs)
        |                 |                    |
        |                 |                    |
        v                 v                    v
Git service         GitHub auth           Repo discovery
(git child          (gh child process,    (filesystem BFS)
processes)           GitHub REST API)
        |                 |
        v                 v
Bundled/system Git   OS credential store through gh
```

The trust boundary is the preload bridge. The renderer is unprivileged and must
remain that way.

### 5.1 Runtime process responsibilities

**Renderer (`app/page.tsx`)**

- Owns all visual state and interaction state.
- Renders the title bar, toolbar, repository sidebar, changes/history views,
  modals, popovers, toasts, and all SVG icons.
- Calls only methods exposed on `window.relayDesktop`.
- Never reads the filesystem, runs Git, calls `gh`, or handles credentials.
- Converts raw unified diff text into renderable line records.

**Preload (`electron/preload.cjs`)**

- Runs with access to Electron IPC.
- Exposes the smallest practical API through `contextBridge`.
- Translates invoke calls and subscribes to two event streams:
  `relay:github-login-progress` and `relay:menu-action`.
- Adds `desktop` and platform classes to the document root after DOM load.

**Main process (`electron/main.cjs`)**

- Creates and secures the BrowserWindow.
- Owns native dialogs and application menus.
- Validates IPC inputs.
- Reads and atomically writes Relay's public metadata store.
- Resolves active and per-repository accounts.
- Retrieves OAuth tokens only when a Git operation or GitHub API request needs
  one.
- Coordinates Git, GitHub CLI, GitHub API, and repository discovery services.

**Git service (`electron/git-service.cjs`)**

- Locates bundled Git or falls back to `git` on `PATH`.
- Constructs a correct relocatable environment for bundled Git.
- Executes Git without a shell via `execFile`.
- Parses status, history, branches, ahead/behind counts, and diffs.
- Performs clone, commit, fetch, push, and branch switching.

**GitHub auth service (`electron/github-auth.cjs`)**

- Locates bundled `gh` or falls back to `gh` on `PATH` for source development.
- Isolates Relay's GitHub CLI configuration under the Electron user-data folder.
- Lists authenticated accounts, starts browser login, switches accounts, logs
  out, and retrieves a token for one named account.
- Scrubs inherited `GH_TOKEN`, `GITHUB_TOKEN`, and `GH_HOST` variables so a
  developer's terminal environment cannot silently select the wrong account.

**Repository discovery (`electron/repository-discovery.cjs`)**

- Performs a breadth-first directory traversal.
- Detects ordinary `.git` directories and linked-worktree `.git` files.
- Avoids symlinks, `.git`, and `node_modules` directories.
- Stops descending after finding a repository root.
- Tolerates unreadable and disappearing directories.
- Returns at most 5,000 sorted repository paths.

## 6. Authoritative File Map

### Desktop product

| Path | Purpose |
| --- | --- |
| `app/page.tsx` | Entire React application, renderer types, icons, state, and actions |
| `app/globals.css` | Complete visual system and desktop-responsive layout |
| `desktop/index.html` | Vite renderer HTML entry |
| `desktop/main.tsx` | React root that renders `Home` from `app/page.tsx` |
| `vite.desktop.config.ts` | Desktop Vite build; outputs to `desktop-dist/` |
| `electron/main.cjs` | Electron lifecycle, menus, persistence, GitHub REST calls, IPC |
| `electron/preload.cjs` | Context bridge and renderer-safe API |
| `electron/git-service.cjs` | Git discovery, execution, parsing, and mutations |
| `electron/github-auth.cjs` | Multi-account GitHub CLI OAuth integration |
| `electron/repository-discovery.cjs` | Recursive local repository scanner |
| `electron/repository-order.cjs` | Sidebar ordering rules and backward-compatible store normalization |
| `electron/ssh-service.cjs` | SSH identities for non-GitHub hosts, remote parsing, and the connection test |
| `electron/application-menu.cjs` | The one menu definition behind both the native menu and the Windows in-window menu bar |
| `build/icon.icns` | Generated macOS icon; referenced by electron-builder |
| `build/icon.ico` | Generated Windows icon; referenced by electron-builder |
| `build/icon.png` | Generated 1024px icon, kept as a plain raster of the master |
| `build/icon.svg` | Editable master for the application icon at 64px and above |
| `build/icon-small.svg` | Editable master for 48px and below, with heavier strokes and no shadow |
| `build/generate-icons.cjs` | Regenerates all three generated icons; run with `npm run icons:build` |
| `package.json` | Scripts, dependencies, Electron entry, packaging configuration |
| `tests/rendered-html.test.mjs` | Current server-render and architecture regression tests |
| `TODO.md` | Explicitly requested product backlog; items are not implemented until verified |

### Hosted/scaffolding path

| Path | Purpose and warning |
| --- | --- |
| `app/layout.tsx` | Vinext/Next metadata and font setup; not loaded by desktop Vite entry |
| `app/chatgpt-auth.ts` | Hosted ChatGPT authentication helper; not used by Electron |
| `vite.config.ts` | Vinext/Cloudflare build, not the desktop Vite build |
| `worker/index.ts` | Cloudflare worker entry, not Electron main |
| `db/`, `drizzle/`, `drizzle.config.ts` | Hosted database scaffolding; not desktop persistence |
| `examples/` | Scaffold examples; not production desktop code |
| `build/sites-vite-plugin.ts` | Packages hosted-site metadata; unrelated to Electron packaging |

Before deleting scaffolding, verify whether tests or hosting workflows still
depend on it. Before implementing a desktop request, start in `app/page.tsx` and
`electron/`, not the hosted stack.

### Generated and local-only paths

| Path | Treatment |
| --- | --- |
| `node_modules/` | Generated dependencies; never commit |
| `desktop-dist/` | Generated desktop renderer; rebuilt by `desktop:build` |
| `dist/`, `.next/`, `.vinext/`, `.wrangler/` | Generated hosted build state |
| `outputs/` | electron-builder installers and unpacked apps; publish as release assets, not Git files |
| `work/` | Local integration fixtures and temporary profiles; never publish |
| `runtime/` | Large third-party Git and GitHub CLI runtimes; intentionally ignored |
| `.openai/` | Local Sites metadata; intentionally ignored |

## 7. Startup and Application Lifecycle

1. Electron waits for `app.whenReady()`.
2. `registerIpc()` installs every `relay:*` invoke handler.
3. `installApplicationMenu()` installs native File/Edit/View/Window menus.
4. `createWindow()` creates a hidden BrowserWindow.
5. The window loads `desktop-dist/index.html` from disk.
6. The preload adds platform CSS classes and exposes `window.relayDesktop`.
7. React mounts and calls `getState()`.
8. `getState()` calls `syncGitHubAccounts()` before returning public state.
9. The window becomes visible on `ready-to-show`.

The BrowserWindow security configuration is intentional:

```text
contextIsolation: true
nodeIntegration: false
sandbox: true
```

Do not weaken these settings to make renderer code easier. Add a narrow IPC
method instead.

The BrowserWindow also:

- Denies renderer-created windows.
- Sends HTTPS links to the system browser.
- Blocks non-`file://` navigation in the renderer.
- Uses a hidden-inset title bar on macOS.
- Keeps the Windows application menu visible.

On macOS the app stays alive after the last window closes and recreates a window
on activation. On Windows it quits when all windows close.

## 8. Persistence Model

Relay stores public application metadata in:

```text
<Electron userData>/relay-data.json
```

Typical locations are:

- macOS: `~/Library/Application Support/relay-desktop/relay-data.json`
- Windows: `%APPDATA%\relay-desktop\relay-data.json`

The GitHub CLI config for Relay lives beside it:

```text
<Electron userData>/github-cli/
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
`electron/repository-order.cjs` and `normalizeSshState()` in `main.cjs`. A store
written before those fields existed is upgraded in place: `repositoryOrder` and
`manualOrder` gain defaults, `manualOrder` is seeded from the existing
repository list so nothing is reshuffled, and `addedAt` backfills from
`lastOpened`, which is never later than the true first-added time. Nothing is
removed, so an older Relay can still read the file.

An SSH profile holds a host, an optional user and port, and an optional *path*
to a private key. It never holds key contents or a passphrase.

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
device code from cleaned CLI output, emits only the sanitized code and fixed
verification URL to the renderer, and has the Electron main process open
`https://github.com/login/device` exactly once per login attempt. If the system
browser cannot be opened, login keeps running and the renderer retains a manual
open button.

The device code remains visible in a read-only, selectable input. Its copy
button awaits the Clipboard API and shows `Copied` only after the write
succeeds; failures leave a manual-copy path instead of reporting false success.

The OAuth credential is stored by GitHub CLI in the operating system credential
store. Relay's JSON store contains only account metadata.

### 9.2 Account synchronization

`syncGitHubAccounts()`:

1. Reads authenticated GitHub CLI accounts.
2. Reuses known metadata by case-insensitive handle.
3. Fetches `/user` only for newly observed accounts.
4. Builds a stable account ID from the numeric GitHub user ID.
5. Selects the CLI-active account, or the first account if none is marked active.
6. Removes repository bindings whose account no longer exists.
7. Persists and returns public state.

### 9.3 Account switching

Switching the active Relay account calls `gh auth switch`. It is not merely a
React selection. This keeps GitHub CLI's active account and Relay's active
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

### 9.6 SSH identities for non-GitHub hosts

GitHub accounts and SSH identities are deliberately separate models. An account
is an OAuth identity held by the GitHub CLI; an SSH profile is only a hint about
which key to offer a host. Do not model a non-GitHub host as a GitHub account,
and do not add a personal access token for one.

With no profile bound to a repository, Relay sets nothing, so the user's SSH
agent and `~/.ssh/config` behave exactly as they do for `git` on the command
line. That is the default and covers most setups.

When a profile is bound, `sshCommandFor()` builds a `GIT_SSH_COMMAND` for that
single Git invocation:

```text
'ssh' -i '<identityFile>' -o 'IdentitiesOnly=yes' [-p <port>]
```

`IdentitiesOnly` is what lets several identities share one host; without it
OpenSSH offers every agent key and the server closes the connection after too
many attempts. Paths are POSIX single-quoted, including on Windows, because Git
hands `GIT_SSH_COMMAND` to a shell.

Two rules keep the credential shapes from crossing:

- `sshCommandForRepository()` returns null for any GitHub remote, and
  `sshEnvironment()` in the Git service attaches `GIT_SSH_COMMAND` only to an
  SSH remote.
- The fetch and push handlers do not even request an OAuth token unless the
  remote is a GitHub remote, and the credential helper remains gated on
  `https://github.com`.

`testConnection()` runs an authentication-only `ssh -T` with `BatchMode=yes`, so
it can never hang on an invisible passphrase prompt, and `describeSshResult()`
turns the outcome into one actionable sentence that names the host without
echoing key paths back to the user.

### 9.7 Token handling

`accountToken()` asks GitHub CLI for one named account token in the main process.
The token is never returned across IPC.

For an HTTPS `github.com` clone, fetch, or push, the Git service:

1. Clears the ordinary Git credential helper for that command.
2. Installs a command-scoped helper that prints username and password.
3. Passes the token through `RELAY_GIT_TOKEN` in the child environment.
4. Sets `GIT_TERMINAL_PROMPT=0` to avoid hanging on an invisible prompt.

Never log the child environment or the constructed credential response. Never
embed a token in the remote URL, Git configuration, command history, renderer
state, error text, or release metadata.

SSH URLs do not use a GitHub OAuth token. They use the user's existing SSH
configuration and keys.

## 10. GitHub REST API Usage

The main process uses GitHub REST API version `2022-11-28` and the
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
It returns only the fields the renderer needs. The renderer filters locally and
renders at most the first 250 matching rows at once.

GitHub API requests belong in the main process. Do not add an IPC method that
returns a raw OAuth token so the renderer can call GitHub directly.

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
- Porcelain v1 status, including untracked files
- `origin` URL if present
- Existing local branches
- Up to 30 history entries, used only as a HEAD signal for the History tab
- Parsed changed files and approximate line counts
- Ahead/behind values relative to the upstream or matching remote branch
- Whether a configured upstream exists
- `latestCommit`, the committer date of `HEAD`, or `null` with no commits

GitHub owner/name is inferred from HTTPS or SSH GitHub remotes. For non-GitHub
or missing remotes, owner falls back to the parent folder and name to the root
folder.

### 12.2 Status parsing

The renderer model reduces Git status to `A`, `M`, or `D` with tones
`added`, `modified`, and `deleted`. It does not preserve every two-character Git
status combination.

Untracked text files smaller than 2 MiB get an approximate added-line count by
reading the file. Binary and large files retain zero cosmetic line counts.

### 12.3 Diffs

- Tracked files use `git diff HEAD --no-ext-diff --unified=3`.
- A staged-diff fallback is used when the first command fails.
- Untracked text files are represented as a synthetic all-added unified diff.
- Untracked binary files return `Binary file — preview unavailable`.
- The renderer parses hunk headers and tracks old/new line numbers.

### 12.4 History reads

The History tab does not use the 30 entries in the repository payload. It calls
three narrow methods instead, so opening a repository never pays for its whole
history.

- `readHistoryPage()` returns one batch. Paging is anchored to an explicit
  commit resolved on the first page, not to `HEAD`, so a `HEAD` that moves
  while the user scrolls cannot make later pages skip or repeat commits. It
  reports `endOfHistory` rather than imposing a cap.
- `readCommitDetail()` returns full and short hashes, subject and body,
  separate author and committer identities, parents, decorations, and the
  changed files with their add/modify/delete state and line counts. A merge is
  compared against its **first parent**; a root commit has no parent and is
  compared against the empty tree, so its files read as added.
- `readCommitFileDiff()` returns one file's diff against that same parent.

Commit hashes and file paths arrive over IPC and are untrusted.
`assertCommitHash()` checks the shape and `assertCommitInRepository()` runs
`git cat-file -e <hash>^{commit}` against the open repository, so a real hash
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

### 12.6 Fetch and push

- Fetch runs `git fetch origin --prune`.
- Push runs `git push --set-upstream origin HEAD`.
- Both accept an optional SSH command, applied only to an SSH remote.
- Fetch/push require an `origin` remote.
- Push requires a named local branch.
- The renderer chooses fetch when there is nothing to publish, otherwise push.

### 12.7 Clone

Clone runs:

```text
git clone --progress -- <remoteUrl> <destinationPath>
```

The main process validates:

- URL starts with `http://`, `https://`, `ssh://`, or `git@`.
- Parent folder is supplied and exists.
- Repository folder name is one basename, not `.` or `..`.
- Destination does not already exist.

After a successful clone, Relay fully reads and remembers the new repository.

### 12.8 Branch switching

Relay currently switches only to an existing local branch using
`git switch <branch>`. Input rejects characters outside word characters,
period, slash, and hyphen.

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

The main process reads discovered repositories in batches of 10. Individual
unreadable or invalid repositories are ignored rather than failing the complete
scan. The response distinguishes:

- `found`: filesystem markers found
- `readable`: markers that produced a valid repository summary
- `added`: readable repositories not already remembered

The scanner must remain non-destructive.

## 14. IPC Contract

Every new privileged feature normally requires changes in three places:

1. Register a handler in `electron/main.cjs`.
2. Expose a narrow bridge method in `electron/preload.cjs`.
3. Add or update the `RelayDesktop` TypeScript type in `app/page.tsx`.

Current invoke channels:

| Channel | Renderer method | Input | Result / behavior |
| --- | --- | --- | --- |
| `relay:get-state` | `getState()` | none | Synchronizes GitHub accounts and returns `AppState` |
| `relay:select-repository` | `selectRepository()` | none | Native folder picker, repository read, or `null` |
| `relay:choose-clone-directory` | `chooseCloneDirectory()` | none | Native parent-folder picker or `null` |
| `relay:list-github-repositories` | `listGitHubRepositories(accountId)` | account ID | Sanitized GitHub repository list |
| `relay:clone-repository` | `cloneRepository(input)` | URL, parent, name, account | Repository and updated app state |
| `relay:scan-folder` | `scanFolder()` | none | Native picker plus scan counts/state or `null` |
| `relay:remove-repository` | `removeRepository(path)` | absolute path | Updated app state; never deletes disk data |
| `relay:open-repository` | `openRepository(path)` | absolute path | Full repository model |
| `relay:refresh-repository` | `refreshRepository(path)` | absolute path | Full repository model |
| `relay:get-file-diff` | `getFileDiff(repo, file)` | repository and file paths | Unified diff text |
| `relay:read-history` | `readHistory(path, options)` | path, skip, limit, anchor | One batch of commits plus `anchor` and `endOfHistory` |
| `relay:read-commit` | `readCommit(path, hash)` | path and commit hash | Full commit metadata and changed files |
| `relay:read-commit-diff` | `readCommitDiff(path, hash, file)` | path, hash, file path | That file's diff in that commit |
| `relay:commit` | `commit(input)` | selected files/message/account | Refreshed repository |
| `relay:fetch-origin` | `fetchOrigin(path, accountId)` | repository and optional account | Refreshed repository |
| `relay:push-origin` | `pushOrigin(path, accountId)` | repository and optional account | Refreshed repository |
| `relay:switch-branch` | `switchBranch(path, branch)` | repository and branch | Refreshed repository |
| `relay:connect-account` | `connectAccount()` | none | Browser OAuth then updated state |
| `relay:set-active-account` | `setActiveAccount(id)` | account ID | Switches `gh` account and returns state |
| `relay:set-account-email` | `setAccountEmail(id, email)` | account ID and email | Updated public state |
| `relay:set-repository-account` | `setRepositoryAccount(path, id)` | path and nullable account | Updated public state |
| `relay:remove-account` | `removeAccount(id)` | account ID | Logs out locally and returns state |
| `relay:set-repository-order` | `setRepositoryOrder(mode, direction)` | sort mode and direction | Updated public state |
| `relay:set-manual-order` | `setManualOrder(paths)` | ordered repository paths | Updated public state |
| `relay:save-ssh-profile` | `saveSshProfile(profile)` | SSH profile without key material | Updated public state |
| `relay:remove-ssh-profile` | `removeSshProfile(id)` | profile ID | Updated public state; no key file is touched |
| `relay:set-repository-ssh-profile` | `setRepositorySshProfile(path, id)` | path and nullable profile | Updated public state |
| `relay:test-ssh-profile` | `testSshProfile(profile)` | SSH profile | `{ ok, message }` with no sensitive material |
| `relay:get-menu` | `getMenu()` | none | Platform flag and the menu descriptor |
| `relay:menu-command` | `runMenuCommand(id)` | menu command ID | Runs the command; rejects unknown IDs |
| `relay:open-external` | `openExternal(url)` | HTTPS URL | Opens system browser |

Current main-to-renderer event channels:

| Channel | Preload subscription | Purpose |
| --- | --- | --- |
| `relay:github-login-progress` | `onGitHubLoginProgress` | Device code and login status |
| `relay:menu-action` | `onMenuAction` | Native File-menu commands |

Validate all IPC inputs in the main process even if the renderer already
validates them. The renderer is not the security boundary.

## 15. Native Menus

`electron/application-menu.cjs` holds one menu definition. Both the native
Electron template and the descriptor the renderer draws on Windows are derived
from it, so the two cannot drift; a test asserts they agree.

On macOS the system menu bar is the only menu. On Windows the native menu bar
would occupy a second chrome row, so it is hidden with `autoHideMenuBar` and
`setMenuBarVisibility(false)` while remaining **installed**, which is what keeps
its accelerators working, and the renderer draws the same menus on the title
row. That in-window bar carries `menubar`/`menu` roles, mnemonic underlines,
Alt to open, arrow-key navigation, and Escape to close, and routes every command
through `relay:menu-command`, which accepts only IDs present in the definition.

The File menu contains:

- Add Local Repository… (`CmdOrCtrl+O`)
- Clone Repository… (`CmdOrCtrl+Shift+O`)
- Scan Folder for Repositories…
- Remove Current Repository from Relay
- Exit on Windows

Menu clicks send a small action string to the renderer. The renderer stores the
latest action closures in `menuActionsRef` so the one-time IPC subscription does
not retain stale React state.

When adding a menu action:

1. Add the item to `MENU_DEFINITION` in `electron/application-menu.cjs`, and
   extend `runMenuCommand()` in `electron/main.cjs` if it is not a role.
2. Extend `MenuAction` in `app/page.tsx` if the renderer handles it.
3. Add the action callback in `menuActionsRef.current`.
4. Verify both macOS and Windows labels/accelerators.
5. Extend the architecture regression test if the action is important.

## 16. Renderer State Model

The renderer uses local React state rather than a separate state library.

Persistent/public domain state:

- `appState`: accounts, active account, recent repositories, and bindings
- `repository`: the full currently opened repository; deliberately not restored
  at startup

Repository interaction state:

- Active Changes/History tab
- Active changed file
- Selected commit files
- Diff text
- Commit summary and description
- Repository search

Transient UI state:

- Loading and string-valued `busy` operation state
- Toast notice
- Account popover
- Add/manage account modals
- Repository account settings modal
- Clone modal, source tab, selected GitHub repository, clone form, and list state
- Account email modal
- GitHub login progress and transient device-code copy feedback

`applyRepository()` is the central renderer-side normalization point. It:

- Sets the current repository.
- Moves its summary to the front of recent repositories.
- Caps the local list at 5,000.
- Preserves only selected files that still exist when requested.
- Selects the first changed file when an old selection is invalid.

The main process remains authoritative for persisted recent repositories. The
renderer performs matching optimistic list updates to keep the UI responsive.

### 16.1 Refresh behavior

When the window regains focus, Relay refreshes the current repository unless an
operation is already busy. Individual actions also refresh after Git mutations.

### 16.2 Popover dismissal

The account popover uses an `accountMenuRef` and a document-level `pointerdown`
listener installed only while open. A click inside the wrapper does not close
it; a click elsewhere does. Preserve cleanup in the effect return function.

### 16.3 Error normalization

`messageFrom()` strips Electron's `Error invoking remote method` wrapper and a
leading `Error:`. Main-process errors should be human-readable because they are
shown directly in a toast.

## 17. Visual and Interaction System

The UI deliberately resembles GitHub Desktop structurally without copying its
source or branding.

Key layout:

```text
title row (Windows only): Relay | File Edit View Window | window controls
repository action row: current repository | current branch | fetch/push  ...  active account
workspace:
  repository sidebar (ordering control, then the repository list)
  main panel:
    Changes tab: file list + commit box | diff viewer
    History tab: commit list | commit detail, changed files, diff
status bar: signed-in identity | repository account settings
```

macOS has no title row: its window controls sit in the repository action row,
which is the drag region. Windows keeps a title row with the menus on it and
the native window controls in a title bar overlay.

Typography reads from the tokens at the top of `app/globals.css`. Primary
control and body text is `--text-body` (13px) and secondary metadata is
`--text-meta`/`--text-sm` (11-12px). `--text-badge` (10px) is the only size
below 11px and belongs only on compact, non-essential badges. Do not reintroduce
one-off pixel sizes.

Design rules:

- Keep controls left-aligned where practical.
- Use the green color tokens as restrained action/selection accents.
- Avatars use flat solid coral, violet, or blue backgrounds.
- Icons live in the `Icon` component and are inline SVG paths.
- Add new icons to `IconName` and the `Icon` switch; do not substitute Unicode
  symbols or emoji.
- Avoid icon gradients and highlight effects.
- General panel/modal shadows are allowed; the "flat icons" rule does not ban
  all depth from the application.
- Maintain accessible names for icon-only buttons.
- Modal backdrops close on direct backdrop clicks unless an operation must not
  be interrupted.
- At widths below 720px the repository sidebar hides and the changes view may
  scroll horizontally. Electron's minimum window width is currently 820px, so
  this is mainly defensive and hosted-preview behavior.

The desktop root gets `desktop` plus `macos`, `windows`, or `linux`. Desktop CSS
removes hosted-page padding and borders, enables title-bar dragging, opts all
interactive controls out of the drag region, and leaves space for macOS traffic
lights.

## 18. Development Commands

Install dependencies:

```bash
npm install
```

Build the desktop renderer only:

```bash
npm run desktop:build
```

Regenerate the application icons after editing either icon master:

```bash
npm run icons:build
```

Build and open Electron:

```bash
npm run desktop:open
```

Lint source:

```bash
npm run lint
```

Run the current test suite:

```bash
npm test
```

Build production installers:

```bash
npm run desktop:mac
npm run desktop:windows
```

Build both configured targets:

```bash
npm run desktop:release
```

Hosted/scaffolding commands:

```bash
npm run dev
npm run build
npm run start
```

Use `desktop:*` commands for desktop-product requests. `npm test` currently
runs the Vinext build before Node tests, so it validates server rendering and
source wiring but does not launch Electron.

## 19. Testing and Verification Strategy

Match verification effort to risk.

### 19.1 Minimum for renderer-only changes

```bash
npm run lint
npm run desktop:build
```

Also visually inspect the relevant state in Electron. For interaction changes,
exercise the actual click sequence rather than relying only on static markup.

### 19.2 Minimum for main/preload/service changes

```bash
npm run lint
node --check electron/main.cjs
node --check electron/preload.cjs
node --check electron/git-service.cjs
node --check electron/github-auth.cjs
node --check electron/repository-discovery.cjs
npm run desktop:build
```

Then launch Electron and exercise the IPC route through the renderer or preload
API. A direct unit call to a service does not prove packaged paths or IPC wiring.

### 19.3 Git integration fixtures

Use temporary repositories under `work/` or a directory from `mktemp -d`.
Cover as relevant:

- Clean repository
- Modified tracked file
- Untracked text file
- Binary file
- Repository with no commits
- Repository with and without an `origin`
- Repository with and without an upstream
- Linked worktree whose `.git` marker is a file
- Folder tree containing multiple repositories
- HTTPS clone of a small public GitHub repository

Never point destructive tests at a user's real repository.

### 19.4 Authentication tests

- Do not print tokens.
- Prefer testing account counts and sanitized field shapes.
- Test with a disposable Electron user-data directory when login state is not
  required.
- When real account state is required, perform read-only checks or write back the
  exact existing value.
- Verify the account popover outside-click behavior.
- Verify custom email persistence without changing authentication identity.

### 19.5 Packaging verification

For macOS:

- Confirm app, bundled Git, and bundled `gh` are Mach-O arm64.
- Launch the packaged `.app`, not only development Electron.
- Confirm File menu entries.
- Confirm the initial state has no open repository.
- Perform a real public HTTPS clone through the packaged IPC path.
- Validate the DMG with `hdiutil verify`.

For Windows when building from macOS:

- Confirm `Relay.exe`, bundled `git.exe`, and `gh.exe` are PE x86-64 payloads.
- Confirm `git-remote-https.exe`, templates, and CA bundle are packaged.
- Inspect the NSIS artifact and unpacked resources.
- A final smoke test on an actual Windows x64 machine is still preferable.

### 19.6 Current automated tests

`tests/rendered-html.test.mjs` checks, against temporary Git fixtures where
real repository behavior is involved:

- Relay server-renders in the no-repository state.
- Old demo text such as `git-fixture` and `All systems operational` is absent.
- Important native menu and IPC strings remain wired.
- Git runtime environment and clone code exist.
- Worktree scanning exists.
- GitHub repository picker exists.
- Account menu outside-click handling exists.
- Repository removal copy promises files remain untouched.
- CSS contains no linear gradients.
- A store written before ordering existed upgrades without reshuffling, and the
  manual order stays consistent with the remembered repositories.
- The `HEAD` commit date used for sorting reads, including `null` with no commits.
- History pages without gaps, duplicates, or a cap, and describes root, merge,
  and deletion commits correctly.
- Commit hashes that are malformed, absent, or from another repository are
  refused.
- The native menu and the in-window menu bar come from one definition.
- There is one repository action row and no duplicate account pill.
- The commit-email modal submits from the keyboard exactly once.
- SSH remote parsing, GitHub separation, and failure messages that leak nothing.
- A clone, fetch, and push over a real Git SSH transport, asserting no GitHub
  credential appears in the exchange.

These are regression guards, not a complete integration suite. They do not
launch Electron and do not cover Windows.

## 20. Packaging and Release Model

electron-builder packages only:

```text
desktop-dist/**/*
electron/**/*
package.json
```

It additionally copies platform-specific Git and GitHub CLI trees from
`runtime/` into the app resources.

Icons are supplied as real containers, `build/icon.icns` and `build/icon.ico`,
not as a single PNG for electron-builder to convert. Its conversion put a 512px
image in the `ic13` (128@2x) slot and a 1024px image in `ic14` (256@2x), so
macOS rescaled the icon at every Retina size. Regenerate with `npm run
icons:build` after editing either master; the output is byte-stable across runs.

Configured artifacts:

- `Relay-<version>-arm64.dmg`
- `Relay-Setup-<version>-x64.exe`

Output directory:

```text
outputs/installers/
```

The NSIS installer is interactive, allows choosing an install directory, and
creates desktop and Start Menu shortcuts.

### 20.1 Release checklist

1. Confirm the intended version.
2. Update both `package.json` and `package-lock.json`.
3. Run lint, syntax checks, tests, and desktop build.
4. Build the Apple Silicon DMG.
5. Build the Windows x64 installer.
6. Launch and smoke-test the packaged macOS app.
7. Inspect Windows payload architecture and runtime files.
8. Verify the DMG checksum/container.
9. Compute SHA-256 hashes.
10. Ensure only the intended current installers are in the user-facing output
    location.
11. Commit and push source only when explicitly authorized.
12. Create a GitHub release and upload installers as release assets, not Git
    blobs.
13. Verify release asset names, sizes, upload state, and GitHub-reported digest.

Current builds are unsigned. Do not claim otherwise. macOS Gatekeeper and
Windows SmartScreen may display first-run warnings. Proper signing requires:

- Apple Developer ID signing and notarization for macOS
- A trusted code-signing certificate for Windows

## 21. Common Change Recipes

### Add a new privileged renderer operation

1. Define the smallest possible main-process handler.
2. Validate every external value in the main process.
3. Keep secrets and filesystem objects out of the response.
4. Expose one preload method.
5. Extend `RelayDesktop` types.
6. Add renderer state and UI.
7. Add an automated wiring assertion.
8. Exercise it through packaged or development Electron.

### Add a Git operation

1. Implement it in `electron/git-service.cjs`.
2. Use argument arrays and `--` separators.
3. Decide whether it needs a repository path or a parent working directory.
4. Decide whether GitHub HTTPS credentials apply.
5. Set `GIT_TERMINAL_PROMPT=0` for noninteractive network operations.
6. Return a refreshed repository model after mutations.
7. Test no-remote, no-branch, auth-failure, and ordinary success cases.

### Add account metadata

1. Add a safe default in `syncGitHubAccounts()`.
2. Preserve known stored values during synchronization.
3. Ensure removing an account cleans dependent bindings.
4. Never store secrets.
5. Keep old stores readable by merging defaults or normalizing missing fields.

### Add a native menu item

Follow the four-layer path:

```text
Electron Menu item
  -> relay:menu-action event
  -> MenuAction union
  -> menuActionsRef callback
```

### Add an icon

1. Add a semantic name to `IconName`.
2. Add a 24x24 SVG path in `Icon`.
3. Use `currentColor` and the common stroke settings.
4. Add an accessible label to the containing button.
5. Do not use emoji or font glyph substitutes.

### Change persistence

1. Keep `emptyStore()` backward compatible.
2. Normalize untrusted/missing fields in `readStore()`.
3. Preserve atomic write behavior.
4. Explicitly decide whether a field belongs in `publicState()`.
5. Confirm the field is not sensitive.
6. Add cleanup for references to removed accounts or repositories.

## 22. Security Checklist

Before completing a change, ask:

- Does any token cross into the renderer?
- Could an inherited `GH_TOKEN` select a different account?
- Could an IPC path escape the user's intended repository or clone parent?
- Are user-controlled Git arguments separated from options?
- Could a command prompt invisibly and hang?
- Could an error or log contain a token?
- Could an external URL use a non-HTTPS scheme?
- Could a GitHub token reach a host that is not github.com?
- Could a private key path, key contents, or a passphrase reach the store, the
  renderer, a log, or an error message?
- Could a commit hash or file path from IPC address an object outside the open
  repository?
- Could a renderer navigation replace the local app?
- Does repository removal touch disk data?
- Does a public commit include generated installers, runtime binaries, local app
  data, credentials, `.env` files, or `.openai` metadata?

Keep `contextIsolation`, sandboxing, and disabled Node integration intact.

## 23. Error Handling Conventions

- Service errors should contain one actionable sentence where possible.
- `git-service.cjs` removes a leading `fatal:` from Git stderr.
- GitHub CLI errors use the last non-empty cleaned output line.
- Authentication expiry should tell the user to sign in again.
- A canceled native dialog returns `null`, not an exception.
- Bulk scanning reports unreadable counts instead of failing everything.
- UI actions clear their `busy` state in `finally` blocks.
- Toasts last longer for errors than success messages.

Do not swallow errors that make an operation appear successful. It is acceptable
to ignore best-effort cosmetic data such as line statistics or automatic focus
refresh failures.

## 24. Known Limitations and Technical Debt

Agents should understand these before extending the project:

- `app/page.tsx` is a large monolithic component containing types, icons, state,
  actions, and markup. Feature work may justify extraction, but avoid a broad
  rewrite when a focused change is safer.
- IPC types are manually duplicated between preload behavior and renderer types;
  there is no shared schema or runtime validator.
- Many repository paths accepted by IPC are only lightly validated. The current
  threat model assumes a trusted local renderer, though BrowserWindow hardening
  reduces exposure.
- Folder scanning runs in the Electron main process and has no incremental
  progress events or cancellation.
- Scanning and recents are capped at 5,000 repositories.
- The GitHub repository picker renders only the first 250 filtered items.
- Only GitHub.com is supported. Host is hard-coded in auth and API logic.
- OAuth injection applies only to HTTPS GitHub remotes. SSH relies on the user's
  SSH environment; other HTTPS hosts receive no Relay credential.
- Relay supports fetch and push but not pull, merge, rebase, stash, discard,
  reset, tag, remote management, or conflict resolution.
- History paging uses `--skip`, which Git resolves by walking, so a very deep
  history gets slower the further the user scrolls.
- History search filters only the commits already loaded, not the whole history.
- A merge commit is always compared against its first parent; there is no
  parent selector.
- SSH identities cover the transport only. Relay does not create keys, add them
  to an agent, edit `~/.ssh/config`, or handle passphrases.
- The Windows in-window menu bar is Relay's own. Its accessibility relies on
  ARIA roles rather than the native menu, which stays installed and hidden so
  its accelerators keep working.
- Branch UI lists and switches local branches only. It does not create branches
  or directly check out remote-only branches.
- Status parsing collapses Git's full index/worktree matrix to A/M/D and has
  limited rename/quoted-path handling. Commit file lists collapse the same way.
- Ahead/behind fallback is approximate when upstream information is incomplete.
- Diffs are text-oriented and not suitable for images or rich binary previews.
- Commit creation has no signing support.
- Git and GitHub CLI third-party runtimes are not reproducibly downloaded by a
  repository script. Release packaging requires a prepared local `runtime/`.
- The current automated suite does not launch Electron and does not test Windows
  directly. Everything added for the Windows title bar overlay, its window
  controls, display scaling, and Windows OpenSSH path handling is unverified on
  a real Windows machine.
- Builds are unsigned and unnotarized.
- Hosted Vinext/Cloudflare/Drizzle scaffolding makes the dependency graph and
  `npm test` heavier than the desktop application alone requires.
- There is no auto-update mechanism.
- There is no crash reporting, telemetry, or structured logging.
- Account avatars are initials, not fetched profile images.

Treat this list as context, not authorization to expand scope. Fix only what the
user requests or what is necessary for a safe implementation.

## 25. Agent Working Agreement

When working in this repository:

1. Read `AGENTS.md`, `README.md`, `package.json`, and the relevant source files.
2. Inspect `git status` before editing. Preserve unrelated user changes.
3. Identify whether the request concerns the Electron desktop product or hosted
   scaffolding.
4. Keep changes focused and preserve the non-negotiable invariants.
5. Do not edit generated output when source changes can regenerate it.
6. Do not commit `runtime/`, `outputs/`, `work/`, `.openai/`, or credentials.
7. Do not perform remote writes without explicit authorization.
8. Test at the narrowest level first, then at the real integration boundary.
9. For high-risk changes, test the packaged app, not only source modules.
10. Document new architecture, IPC, persistence, security, build, or release
    behavior here.
11. Report unsigned-build limitations honestly.
12. Never describe Relay as a demo. It is intended to be a working application.

## 26. Definition of Done

A change is done when all applicable items are true:

- Requested behavior works through the actual user-facing path.
- No product invariant was accidentally regressed.
- Renderer/main/preload boundaries remain secure.
- Credentials remain out of renderer state, logs, disk metadata, and Git.
- Lint passes.
- Syntax checks pass for changed CommonJS modules.
- Desktop renderer builds.
- Relevant automated tests pass.
- Relevant Git or GitHub integration was exercised safely.
- Cross-platform packaging implications were considered.
- Visual behavior was inspected if UI changed.
- `AGENTS.md` and `README.md` remain accurate.
- Only requested local or remote state was changed.
