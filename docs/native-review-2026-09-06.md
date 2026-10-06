# Native continuation and five independent reviews

## Scope and outcome

Continue the existing C++26/Qt client for macOS Apple Silicon and Windows x64.
The owner wants GitHub Desktop-style interaction with multiple accounts as
the main distinction. Normal commit history comes first. The future graph
mode belongs in application settings and is deliberately not exposed before
it exists.

The current source has working settings, ordinary history/file selection,
branch creation and fast-forward pull paths, with substantially hardened
Git/account/UI behavior. It is **not yet a complete 1:1 replacement or a
verified native release**.

## Implemented in this continuation

- Native Settings window under the application menus: General and Accounts,
  persisted refresh-on-focus and diff font size.
- Functional Edit actions and keyboard selection of changes/history/files.
- Create a local branch; pull an origin upstream by fast-forward. Divergence
  fails clearly without resetting or rewriting commits.
- Refresh history after HEAD or branch changes, preserve selected changed
  file and unchanged diff scroll, and clear stale commit information/actions.
- Preserve each repository's draft commit message within the running app;
  prevent editing a submitted draft while the commit runs.
- Cancellable browser/device sign-in with a selectable code, bundled `gh`
  discovery, and preservation of account metadata on CLI failures.
- Serialized repository mutations, stale-response/error guards, and durable
  account bindings even when navigating away during clone.
- Atomic metadata rollback, protection for corrupt stores, and activation
  ordering that keeps the visible and effective repository in agreement.
- Correct file-selective commits for renames, staged deletions and literal
  special filenames; NUL-safe Git path parsing; accurate diff rows and bounded
  previews/process output.
- Scoped GitHub credential helper, separate fetch/push URL routing, and SSH
  profile username consistency.
- Operation notices survive asynchronous completion and restore normal status
  after their display interval; trailing diff newlines do not invent context rows.
- Fixed application content geometry, macOS close/reopen behavior, Windows
  installer shortcut paths, CMake preset compatibility and package checks.

## Five independent adversarial reviews

Reviewers received separate areas and were asked for concrete failure cases,
not approval. Three reviewed first; the other two followed as slots became
available. Findings were implemented and regression tests added before final
integration. No reviewer accessed live credentials or wrote to GitHub.

| Reviewer | Concrete findings addressed |
| --- | --- |
| Git/data integrity | Staged rename retained old path in HEAD; magic pathspec filename could include unselected files; staged deletion could not commit; quoted/newline/rename paths broke parsing; oversized previews and hidden diff failures; push URL needed independent account routing. |
| UI/history | Diff headers confused with actual changed lines; select-all required two clicks; selection/scroll lost on refresh; next draft could be erased; stale commit actions; HTML interpretation of commit text; missing sidebar/file selection; entire content tab widget incorrectly capped at 45 px. |
| Async/accounts | Refresh superseded a mutation's fresh result; old read errors appeared under new selections; successful clone lost binding when navigation changed; failed persistence could change identity behind unchanged UI; stale account-email results. |
| Platforms/packages | NSIS shortcuts pointed at nonexistent `bin/Relay.exe`; stage glob rejected ordinary directories; `otool` header caused self-path rejection; preset schema exceeded minimum CMake; macOS closed instead of remaining available. |
| Independent final integration | Bundled `gh` ignored by entrypoint; failed metadata save could leave UI on A while controller targeted B; non-`git` SSH users rejected by clone controller; SSH profile user affected connection test but not transport. |

## Verification and limits

Verification uses Linux x86-64, Clang 19/C++26, CMake 3.31, and Debian Qt 6.8.2
from an isolated toolchain. The production pin remains Qt 6.11.1. QtTest
fixtures cover actual temporary Git repositories and real UI/controller paths;
no live login or private repository operation is part of this local check.
Final integration: the complete application build succeeded and all 19 CTest
suites passed (0 failures, 8.78 seconds), including the UI smoke test and
10 staged-package verification scenarios. `git diff --check` also passed.

Visual review uses screenshots captured by the main-window tests. Set
`RELAY_SCREENSHOT_DIR` to a local output directory to reproduce Settings and
History captures. The geometry test checks that the history and diff occupy
real space, so a populated model alone cannot hide a blank application again.

Package checks were also exercised with mocked macOS tooling: clean stages
pass; Homebrew linkage, PDB files and dSYM directories are rejected. These
checks do not establish that a real installer runs.

## Remaining release and parity work

- Build and run pinned Qt 6.11.1 on actual macOS arm64 and Windows x64; verify
  native menus, window lifecycle, keyboard access, accessibility and scaling.
- Prepare reproducible Git/gh runtime payloads, produce installers and verify
  two-account OAuth, private clone/fetch/push and migration in each installed
  application. Measure footprint and memory with the documented scenarios.
- Complete merge/conflict resolution, stash/discard/revert/undo, additional
  branch management and remote checkout, local repository creation/publication,
  and binary previews before claiming broad GitHub Desktop parity. The
  [documented GitHub Desktop workflows](https://docs.github.com/en/desktop)
  remain a comparison reference, not a statement that all are implemented.
- Add the graphical all-branch history as a separate tested feature, then
  expose its selection in Settings. The normal list remains the default.
- Signing and notarization remain unconfigured. No native release or remote
  publication was performed during this work.
