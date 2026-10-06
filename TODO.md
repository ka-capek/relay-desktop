# Relay Desktop TODO

This file tracks explicitly requested product work that is not finished. Do not
describe an item as supported until its acceptance criteria have been verified
in installed builds on the relevant platforms, which is why implemented work
with outstanding verification is recorded below rather than removed.

The Electron client has been removed. Items that existed only for it, and the
plan to replace it, are gone from this file; `PLAN.md` and `docs/` keep the
history. The latest verification record is
[`docs/native-completion-2026-09-14.md`](docs/native-completion-2026-09-14.md).
Owner-reported items and their status are in `TODO-Karels.md`.

## Verification still outstanding

Native CI builds and tests both platforms with Qt 6.11.1 and produces the
preview installers. What CI cannot prove is listed here.

### Both platforms, installed builds

- Real multi-account GitHub OAuth, switching, and reuse of credentials from an
  existing 0.5.0 installation.
- Private clone, fetch, pull and push over HTTPS and over SSH with a selected
  identity, against disposable repositories.
- GitHub publication from a new local repository.
- Gitea/Forgejo/GitLab discovery against a real server, and the OS credential
  store roundtrip (Keychain, Credential Manager).
- A real non-GitHub SSH host; tests use a local transport.

### Windows x64

- Menus: Alt access, mnemonics, accelerators, disabled states.
- Display scaling at 100%, 125%, and 150%, and typography at 2560x1440 and
  3840x2160.
- Packaged icon in Explorer, the taskbar, Start Menu, shortcuts, and Setup.
- Path handling with the OpenSSH shipped by Git for Windows.

### macOS

- The packaged icon in Finder, the Dock, the app switcher, and the DMG window.
- Typography at 2560x1440 and 3840x2160.

### Not covered by the implementation

- **History paging** uses `--skip`, which Git resolves by walking, so a very
  deep history gets slower the further the user scrolls.
- **History search** filters only the commits already loaded.
- **Merge commits** are always compared against the first parent.
- **SSH identities** cover the transport only. Relay does not create keys, add
  them to an agent, edit `~/.ssh/config`, or handle passphrases, by design.
- **Remote branch with a local namesake.** Picking `origin/<name>` when a local
  `<name>` exists fails with Git's "already exists" error instead of switching
  to the local branch.

## Distribution packaging

**Problem:** Releases are unsigned previews that need Git and the GitHub CLI
installed separately. The strict packaging path in
`native/packaging/README.md` bundles both and verifies the stage, but has not
produced a release.

### Work

- Provision `runtime/` for both platforms with recorded corresponding-source
  URIs.
- Build both installers with strict packaging from the official Qt
  distribution.
- Decide per tool whether to bundle Git and gh or keep them external; the
  startup check already explains what to install when they are missing.
- Record installer sizes and startup measurements in
  `docs/native-measurements.md`.
- Developer ID signing and notarization on macOS, Authenticode on Windows, once
  certificates exist.

### Acceptance criteria

- A first run on a clean machine works with no manual tool installation, or
  explains exactly what to install and recovers without a restart.
- Installer sizes are recorded.

## Icon generator

**Problem:** `build/icon.icns`, `build/icon.ico`, and `build/icon.png` are
generated from `build/icon.svg` and `build/icon-small.svg`. The generator ran
inside Electron and was removed with it, so the masters can no longer be
re-exported from this repository.

### Work

- Add a generator that does not depend on Electron, for example a small Qt
  tool built by CMake using QtSvg.
- Use `icon-small.svg` for 48px and below and `icon.svg` above that.
- Put every Retina slot at its true pixel size in the `.icns`.
- Keep the output byte-stable across runs.

### Acceptance criteria

- One documented command regenerates all three files.
- Regenerating without editing the masters produces no Git diff.

## Persist pane widths

**Problem:** The sidebar, changes, and history panes resize through splitters,
but their widths reset on every launch.

### Work

- Save and restore splitter state.
- Keep the existing minimum sizes so a pane cannot be dragged shut.
- Provide a keyboard-accessible reset to default.
