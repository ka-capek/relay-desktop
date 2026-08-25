# Relay Desktop TODO

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

- Establish a CMake + C++20 build producing macOS Apple Silicon and Windows x64
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
