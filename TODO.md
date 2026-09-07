# Relay Desktop TODO

Native continuation (2026-09-07): see
[`docs/native-completion-2026-09-07.md`](docs/native-completion-2026-09-07.md)
for the implemented workflows, optional graph, reviews and remaining gaps.
The original packaged-platform gates below remain open until actual installers
are verified. Local Linux tests do not satisfy those gates.

This file tracks explicitly requested product work that is not finished. Do not
describe an item as supported until its acceptance criteria have been verified
in packaged builds on the relevant platforms, which is why implemented work with
outstanding verification is recorded below rather than removed.

## Verification still outstanding

Seven items from this file are implemented and on `main`. This file's rule is
that nothing counts as supported until its acceptance criteria are verified in
packaged builds on the relevant platforms, so what has **not** been verified is
listed here rather than deleted. Everything below was built and checked on
macOS Apple Silicon; none of it has run on Windows x64.

### Windows x64, all items

There is no Windows machine in the development environment used for this work.
Nothing below has been seen running on Windows.

- **Window chrome.** The title bar overlay, the position and behaviour of the
  native minimize/maximize/close controls, window dragging, double-click
  maximize/restore, and Aero Snap.
- **The in-window menu bar.** Mouse and Alt access, keyboard navigation,
  mnemonics, accelerators, disabled states, and screen-reader labels. The
  native menu stays installed and hidden so its accelerators keep working, but
  that has not been confirmed on Windows.
- **Display scaling.** The merged rows at 100%, 125%, and 150%, and at the
  820px minimum window width.
- **Typography.** Font rendering and DPI scaling differ from macOS; the scale
  has not been checked at 2560x1440 or 3840x2160 on Windows.
- **Packaged icon.** Explorer, the taskbar, Start Menu, shortcuts, and the
  installer UI, and Explorer/taskbar icon caches while testing.
- **SSH.** Path handling and the OpenSSH that ships with the bundled Git for
  Windows.
- **Repository ordering** and **history** behaviour.

### macOS, still to confirm in a packaged build

- The packaged icon in Finder, the Dock, the app switcher, and the DMG window,
  accounting for the Dock and Finder icon caches. The generated `.icns` is
  structurally correct, `iconutil` accepts it, and the packaged `.app` embeds
  it byte for byte, but it has not been looked at in the Dock.
- Traffic-light placement, title-bar drag regions, double-click behaviour, and
  fullscreen entry and exit after the chrome consolidation. The window config
  was smoke-tested in real Electron; the traffic lights were not photographed.
- Typography at 2560x1440 and 3840x2160.

### Not covered by the implementation

- **History paging** uses `--skip`, which Git resolves by walking, so a very
  deep history gets slower the further the user scrolls.
- **History search** filters only the commits already loaded, not the whole
  history.
- **Merge commits** are always compared against the first parent; there is no
  parent selector.
- **SSH identities** cover the transport only. Relay does not create keys, add
  them to an agent, edit `~/.ssh/config`, or handle passphrases, by design.
- A **real non-GitHub SSH host** has not been used. The clone, fetch, and push
  tests run against a local Git SSH transport, not a live server.

## Header and chrome refinements

**Problem:** Feedback from a packaged Windows x64 run: the header reads poorly
next to macOS, and the "Current repository" caption is noise on both platforms.
On macOS the window controls now share the single action row, which pushes every
control right by the width of the traffic lights.

### Work

- Remove the "Current repository" caption from the repository picker on both
  platforms. The repository name and owner are the content; the label is not.
- On macOS, give the traffic lights their own slim row again and let the
  repository action row start flush at the left edge, with no indent reserved
  for them. This is the opposite of the consolidation done for 0.5.0 and is
  deliberate: the indent costs more than the row it saved.
- Keep Windows at one title row carrying the Relay name, the menus, and the
  native window controls.
- Replace the "R" placeholder mark with Relay's actual application icon, drawn
  slightly larger than the current mark.
- Re-audit the Windows header against macOS once the above lands, since the
  original report was that Windows looks clearly worse.

### Acceptance criteria

- Neither platform shows a "Current repository" caption.
- On macOS the traffic lights sit on their own row and the action row content
  is flush left.
- The app icon appears in place of the "R" mark at a larger size, and stays
  crisp on a HiDPI display.
- Nothing clips or overlaps at the 820px minimum window width on either
  platform.

## Repository row refinements

**Problem:** The selected repository row carries a green bar down its left edge,
and the change count sits inline after the pinned-account avatar rather than at
a consistent right edge, so counts do not line up down the list.

### Work

- Remove the green selection bar. The selected row keeps its background tint.
- Right-align the change count so counts line up down the list regardless of
  repository name length or whether a pinned-account avatar is present.
- Keep the count clear of the remove button that appears on hover.

### Acceptance criteria

- No green bar on the selected row.
- Change counts share a right edge down the whole list.
- The count and the hover remove button never overlap.

## Show only repositories the account can push to

**Problem:** The clone browser lists every repository the account can see,
including ones it can only read. Cloning a repository that cannot be pushed to
is rarely what the user wants, and it makes the list much longer than useful.

### Work

- Use the `permissions` object the GitHub REST API returns for each repository
  and keep only those with `push` (or `maintain`/`admin`).
- Decide explicitly whether this is a filter the user can turn off or an
  unconditional rule, and make the UI say which.
- Do not add a second API round trip per repository; `/user/repos` already
  returns `permissions`.
- Archived repositories cannot be pushed to even with push permission; decide
  and document how they are treated.
- Say clearly in the picker when repositories were hidden, so a user looking
  for a specific read-only repository is not left confused.

### Acceptance criteria

- The clone browser lists only repositories the active account can push to.
- The behaviour is discoverable rather than silent.
- No extra API request per repository, and pagination still works.

## GitHub profile pictures

**Problem:** Account avatars are generated initials on a flat colour. GitHub
already has the user's avatar.

### Work

- Read `avatar_url` from the GitHub profile response and persist it with the
  account. It is a public URL, not a credential.
- Fetch avatars in the main process, not the renderer, and pass them to the
  renderer as data rather than letting the renderer make network requests.
  The renderer must not gain network access to do this.
- Cache them on disk under the Electron user-data folder so the app does not
  refetch on every launch or depend on being online.
- Keep the initials avatar as the fallback for a failed fetch, an offline
  start, or an account with no avatar. Do not show a broken image.
- Apply them everywhere an avatar appears: the account switcher, the account
  menu, Manage accounts, the commit identity line, and the pinned-account mark
  on repository rows.

### Acceptance criteria

- Connected accounts show their real GitHub picture.
- The app still works offline and falls back to initials cleanly.
- No avatar request is made from the renderer.

## Choose the commit email when adding an account

**Problem:** Connecting an account picks a commit email automatically and the
user only finds out later, under Manage accounts. The choice belongs in the
connect flow.

### Work

- After a successful OAuth login, ask which email the new account should use
  for commits, before returning to the main window.
- Offer the addresses GitHub actually knows for the account, including the
  numeric-ID noreply address, rather than only a free-text field. This needs
  the `user:email` scope; if the scope is unavailable, fall back to the
  noreply address plus a free-text field rather than failing.
- Keep the existing rule that a blank value means the noreply address.
- Reuse the existing validation and the existing email modal where practical
  instead of building a second one.
- Do not block the login on it: if the user dismisses the prompt, the account
  is still connected with the noreply default.

### Acceptance criteria

- Connecting an account asks for the commit email as part of the flow.
- Known GitHub addresses are offered, with noreply always among them.
- Dismissing the prompt still leaves a working, connected account.

## Shrink the installed footprint

**Problem:** The packaged macOS app was 490 MB. Measured:

| Part | Size | Share |
| --- | --- | --- |
| Electron and Chromium | 287 MB | 59% |
| Bundled Git | 148 MB | 30% |
| Bundled GitHub CLI | 37 MB | 8% |
| Relay itself | 17 MB | 3% |

114 MB of the bundled Git was Git Credential Manager's .NET runtime and
git-lfs. Relay disables the credential manager on every network operation and
installs its own command-scoped helper, and never invokes lfs, so roughly a
quarter of the application was a credential manager it actively switches off.

Excluding those at packaging time took the app to 382 MB and the bundled Git to
40 MB, with a real HTTPS clone through the packaged binary still working. That
is done.

### Remaining decision: stop bundling Git and the GitHub CLI

This would remove a further 78 MB, and the code already supports it:
`gitExecutable()` falls back to `git` on `PATH` and `githubCliPath()` falls back
to `gh`, so it is a packaging change rather than a service rewrite.

The cost is the first run. Git is often already present on macOS through the
Xcode command line tools and can be prompted for; the GitHub CLI is rarely
installed on either platform, and it is the smaller of the two at 37 MB.

### Work

- Decide per tool rather than as one switch. Dropping Git is the larger saving
  and the smaller inconvenience; dropping the GitHub CLI saves least and hurts
  most.
- If either is unbundled, detect it at startup and explain precisely what to
  install and how, per platform, rather than failing at the first Git command.
- Check the version found on `PATH` is new enough for the commands Relay uses.
- Keep working when a tool appears or disappears while Relay is running.
- Verify on both platforms, including a Windows machine with no Git installed.

### Acceptance criteria

- A first run with the tool missing explains what to install, and recovers
  without a restart once it is.
- No Git operation fails with a raw "command not found".
- The measured installer size is recorded against the numbers above.

**Note:** even unbundling both leaves Electron's 287 MB, so the footprint goal
is mostly gated on the native rewrite rather than on this item.

## Resizable panes

**Problem:** Every pane is a fixed width: the repository sidebar, the file list,
the commit list. On a wide display the diff is cramped while the sidebar wastes
space, and on a narrow one the reverse.

Deliberately deferred. Raised together with the items above but not wanted yet,
and it interacts with the native rewrite, which would have to implement it a
second time.

### Work

- Make the repository sidebar, the changes file pane, and the history commit
  pane resizable by dragging their dividers.
- Persist each width, and clamp to sensible minimums so a pane cannot be
  dragged shut by accident.
- Provide a keyboard-accessible way to resize, and reset-to-default.

## Replace Electron with a native C++ client

**Problem:** Relay is an Electron application, and the cost is visible in the
shipped product. The 0.4.0 Apple Silicon DMG is roughly 200 MB and the Windows
installer roughly 150 MB, because each one carries a complete Chromium and
Node.js runtime alongside the bundled Git and GitHub CLI trees. The application
is a dense, mostly-native desktop tool — lists, a diff view, native menus — and
it pays for a browser engine it barely uses. Chromium also sets the memory
floor, the cold-start time, and a continuous security-update obligation that
has nothing to do with Relay's own code.

Rewrite Relay as a native C++ desktop application. This is a full product
rewrite, not a refactor, and it invalidates most of the current architecture
documented in `AGENTS.md` sections 5 through 20.

**Toolkit decision:** Qt 6 Widgets, chosen over Qt Quick/QML, wxWidgets, and a
per-platform Cocoa/Win32 split. Widgets gives genuinely native menus, window
chrome, and keyboard behavior on both targets; it handles dense list and
tree views without custom scene-graph work; it has first-class HiDPI and
accessibility support; and `QtSvg` preserves Relay's flat code-native icon
system. QML would make Relay's current visual style easier to reproduce but is
weaker on native menus and dense desktop lists. wxWidgets is lighter but its
custom-drawn view story is poor, which the diff pane needs.

### Work

**Foundation**

- Establish a CMake + C++26 build producing macOS Apple Silicon and Windows x64
  binaries, matching the current supported target list exactly.
- Pin a Qt 6 LTS version. Resolve the licensing position explicitly before any
  release: LGPLv3 requires dynamic linking to Qt and the ability for a user to
  relink against their own Qt build. Do not statically link Qt without a
  commercial licence.
- Decide the source layout and whether the rewrite lives on a branch, in a
  sibling directory, or in a separate repository. Do not delete the working
  Electron application until the native client reaches feature parity.

**Port the service layer first, since its semantics are already specified**

- Port `electron/git-service.cjs` behavior. Keep launching Git as a child
  process initially rather than adopting libgit2: the current relocatable
  bundled-Git environment (`GIT_EXEC_PATH`, `GIT_TEMPLATE_DIR`,
  `GIT_CONFIG_SYSTEM`, `PATH`, `GIT_SSL_CAINFO`) and the exact status, diff,
  history, and ahead/behind parsing are the hard-won part, and reproducing them
  over a new Git binding at the same time as a new UI compounds the risk.
  Revisit libgit2 later, for status and log speed, as a separate change.
- Continue executing Git without a shell, with argument arrays and `--`
  separators. `QProcess` with `setArguments` satisfies this; never build a
  command string.
- Port `electron/github-auth.cjs` unchanged in behavior: the official GitHub CLI
  stays the OAuth mechanism. This preserves the no-PAT invariant and keeps
  credentials in the operating system credential store rather than in Relay.
- Port `electron/repository-discovery.cjs` breadth-first scanning, including the
  linked-worktree `.git`-file case, the symlink and `node_modules` exclusions,
  and the 5,000-repository cap.
- Use `QNetworkAccessManager` for the GitHub REST calls, preserving API version
  `2022-11-28`, the `Link`-header pagination loop, and the practice of never
  handing a token to the presentation layer.

**Persistence and migration**

- Keep the `relay-data.json` format, location, atomic temp-file-and-rename
  write, and `0600` mode, so an existing installation migrates in place with no
  conversion step and a user can move between the two clients during the
  transition.
- Keep `selectedRepositoryPath` forced to null on read, which is how the
  no-repository-at-startup invariant is enforced.

**User interface parity**

- Reproduce the current layout: title bar, repository action row, repository
  sidebar, Changes and History tabs, file list, commit box, diff viewer, status
  bar, modals, the account popover, and toasts.
- Keep the flat SVG icon system through `QtSvg`. Do not substitute emoji, icon
  fonts, or bitmap icons.
- Rebuild the diff view as a custom-painted or model-backed view. A naive
  widget-per-line approach will not hold up on large diffs; this is the single
  most likely place for the rewrite to regress against Chromium.
- Use native `QMenuBar` behavior: the macOS system menu bar and the Windows
  in-window menu, preserving mnemonics, accelerators, and disabled states.

**Invariants**

- Restate all twelve invariants in `AGENTS.md` for a single-process native
  application. Several change meaning and must not be quietly dropped:
  - Invariant 11 currently describes a renderer that cannot reach Node.js or a
    token. A single-process C++ application has no such boundary, so the
    protection has to be re-expressed as a code-level rule about where tokens
    may live and how long they persist, and enforced by review rather than by
    the process model.
  - Invariant 12's HTTPS-only external navigation becomes a rule about
    `QDesktopServices::openUrl` call sites.
- Confirm the token-handling rules still hold: no token in the metadata store,
  in logs, in Git configuration, in error text, or in a remote URL.

**Packaging and release**

- Replace electron-builder: `macdeployqt` plus a DMG step for macOS, and
  `windeployqt` plus NSIS or WiX for Windows.
- Record the resulting installer sizes and cold-start times against the Electron
  baseline, since size and startup are the stated reasons for the rewrite.
- Signing remains out of scope and builds remain unsigned until certificates
  exist. Do not claim otherwise.

**Testing**

- The current `npm test` suite server-renders the React UI through the Vinext
  path. A C++ rewrite deletes that verification strategy outright, so a
  replacement must be designed rather than assumed: Qt Test for unit coverage of
  the ported parsers, plus an integration layer that exercises Git operations
  against temporary fixture repositories.
- Port the existing regression intent: no repository open at startup, removal
  never touching disk data, account popover outside-click dismissal, and custom
  commit-email persistence.

### Open questions

- Effort. This is a multi-month rewrite by a realistic estimate, and the
  Electron client has to keep receiving fixes throughout. Confirm that trade is
  wanted before starting.
- Whether the hosted Vinext/Cloudflare/Drizzle scaffolding is retired at the
  same time or kept as a separate web surface.
- Whether any remaining TODO items should be implemented in the Electron client
  first, or deliberately deferred so they are only ever built once, natively.

### Acceptance criteria

- The native client builds from source on macOS Apple Silicon and Windows x64
  through a single documented command per platform.
- Every flow listed in `AGENTS.md` section 1 works in the native client.
- All twelve product invariants are restated for the native architecture and
  hold in the packaged application.
- An existing `relay-data.json` and GitHub CLI configuration are picked up with
  no migration step and no re-authentication.
- A GitHub OAuth login, a private HTTPS clone, a file-selective commit, a fetch,
  and a push all succeed in packaged builds on both platforms.
- Diff rendering stays responsive on a large diff, with no freeze that the
  Electron client did not also have.
- Installer size and cold-start time are measured and recorded against the
  Electron baseline.
- `AGENTS.md` is rewritten for the native architecture rather than patched.
