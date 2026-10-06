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

- **History paging** reads windows of 2,000 or 5,000 commits; each further
  window still costs one Git walk with `--skip`.
- **History search** returns at most 200 matches.
- **Merge commits** are always compared against the first parent.
- **SSH identities** cover the transport only. Relay does not create keys, add
  them to an agent, edit `~/.ssh/config`, or handle passphrases, by design.

## Distribution packaging

**Problem:** Releases are unsigned previews. Windows installers bundle Git for
Windows and the GitHub CLI, macOS installers bundle the GitHub CLI, through
`native/tools/fetch_runtimes.py`. macOS still needs Git from the Xcode Command
Line Tools or Homebrew, and the strict packaging path in
`native/packaging/README.md`, which verifies the whole stage, has not produced
a release.

### Work

- Decide whether macOS should bundle a Git build (none is published as a
  relocatable binary; it would have to be built and its source offered).
- Build both installers with strict packaging from the official Qt
  distribution, using `fetch_runtimes.py --output runtime`.
- Record installer sizes and startup measurements in
  `docs/native-measurements.md`.
- Developer ID signing and notarization on macOS, Authenticode on Windows, once
  certificates exist.

### Acceptance criteria

- A first run on a clean machine works with no manual tool installation, or
  explains exactly what to install and recovers without a restart.
- Installer sizes are recorded.
