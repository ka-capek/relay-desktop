# Relay Desktop TODO

This file tracks explicitly requested product work that has not been
implemented yet. Do not describe an item as supported until its acceptance
criteria have been verified in packaged builds on the relevant platforms.

## App icon quality in packaged builds

**Problem:** Application icons have been reported as blurry or low-resolution.
The current `build/icon.png` source is 1024x1024, so the source dimensions alone
do not explain the problem. Relay currently supplies that PNG directly to
electron-builder for both platforms instead of maintaining native
multi-resolution `.icns` and `.ico` assets. Packaging conversion, an unsuitable
source design at small sizes, platform icon caches, or Electron's development
icon may be responsible. Confirm the actual cause before changing artwork.

### Work

- Inspect the icon shown by the packaged application, not only development
  Electron.
- Inspect `build/icon.svg` and the 1024x1024 PNG at native and small sizes.
- Generate a macOS `.icns` containing the complete standard and Retina size
  set, and point the macOS electron-builder configuration at it.
- Generate a Windows `.ico` containing appropriate small through 256px variants,
  and point the Windows electron-builder configuration at it.
- Adjust the artwork's padding and small-size details if downsampling is making
  the mark indistinct.
- Account for macOS Dock/Finder icon caches and Windows Explorer/taskbar icon
  caches while testing so a stale icon is not mistaken for a failed build.

### Acceptance criteria

- The packaged Apple Silicon app is crisp in Finder, the Dock, the app switcher,
  and the DMG window.
- The packaged Windows x64 app is crisp in Explorer, the taskbar, Start Menu,
  shortcuts, and installer UI.
- The packaged apps show Relay's icon rather than Electron's default icon.
- Icon appearance remains legible at the smallest platform-rendered sizes.

## SSH identities for non-GitHub remotes

**Problem:** Relay's account UI currently represents GitHub.com identities
authenticated through GitHub CLI OAuth. Non-GitHub Git hosts need explicit SSH
support without being modeled as GitHub OAuth accounts. Although raw SSH clone
URLs can already be passed to Git, Relay has no UI or persisted routing model
for selecting and testing a non-GitHub SSH identity.

### Work

- Design a separate SSH identity/profile model for non-GitHub hosts. Do not add
  fake GitHub accounts or require a personal access token.
- Continue using the user's OpenSSH configuration and SSH agent by default.
- Let a repository select an SSH profile or key when the default agent/config
  is insufficient, including the case where multiple identities use one host.
- Recognize common SSH remotes such as `git@host:owner/repository.git` and
  `ssh://user@host/path/to/repository.git`.
- Provide a safe connection test and actionable authentication errors.
- Keep commit name/email selection separate from transport authentication.
- Ensure GitHub OAuth credentials are never injected into non-GitHub remotes.
- Never copy or persist private-key contents or key passphrases in
  `relay-data.json`, renderer state, logs, or Git configuration. Prefer the OS
  SSH agent and existing `~/.ssh/config` behavior.
- Verify path handling and OpenSSH behavior on macOS Apple Silicon and Windows
  x64.

### Acceptance criteria

- A user can clone, fetch, and push a non-GitHub repository over SSH without a
  PAT.
- Existing SSH agent and `~/.ssh/config` setups work without extra Relay setup.
- A repository can use a chosen SSH identity without affecting unrelated
  repositories.
- Authentication failures identify the host and explain the next safe action
  without exposing sensitive material.
- Private keys, passphrases, and GitHub OAuth tokens never cross into the
  renderer or Relay's metadata store.

## Submit commit-email changes with Enter

**Problem:** The Change commit email modal can currently be confirmed only by
clicking **Save email**. Pressing Enter while focused in its email input does not
submit the change because the controls are not implemented as a form.

### Work

- Make the modal use a semantic form with one submit path shared by the Enter
  key and the Save email button.
- Prevent duplicate submissions while the email save operation is busy.
- Preserve the existing blank-value behavior, which restores the GitHub noreply
  address.
- Preserve current email validation and user-facing error handling.
- Add an automated regression check for keyboard submission.

### Acceptance criteria

- With focus in the commit-email input, pressing Enter performs exactly the same
  save action as clicking **Save email**.
- A successful save closes the modal, updates the displayed email, and shows the
  existing success notice.
- Invalid input remains open and displays an actionable error.
- Repeated Enter presses cannot cause concurrent or duplicate saves.

## Repository ordering and drag-and-drop

**Problem:** Remembered repositories currently appear in Relay's recency-driven
order, and the user cannot choose a sorting rule or arrange the sidebar
manually. Add explicit ordering controls for manual drag-and-drop, repository
age, name, and latest commit.

Unless the user specifies otherwise during implementation, define **age** as
the time a repository was first added to Relay. Do not use filesystem creation
or modification timestamps, which are inconsistent across platforms and can
change when repositories are copied. Define **latest** using the timestamp of
the current `HEAD` commit. Repositories with no commits belong after repositories
with commits.

### Work

- Add an ordering control near the repository list with these modes:
  - Manual
  - Age
  - Name
  - Latest commit
- Support useful ascending and descending directions for the sortable modes:
  oldest/newest for age, A-Z/Z-A for name, and newest/oldest for latest commit.
- Allow drag-and-drop only in Manual mode, or clearly switch to Manual mode when
  a drag begins. Do not present a reorder interaction whose result is
  immediately overridden by an active automatic sort.
- Persist the selected ordering mode, direction, repository `addedAt` value, and
  manual order in Relay's metadata store using backward-compatible defaults.
- Preserve an existing repository's original `addedAt` value when it is opened,
  refreshed, rediscovered by a folder scan, or moved in the manual order.
- Obtain the latest commit timestamp efficiently when reading repository
  summaries. Avoid loading full histories merely to sort the sidebar.
- Use stable tie-breakers so equal values do not cause repositories to jump
  unpredictably. Prefer case-insensitive natural name order, followed by the
  canonical repository path.
- Ensure adding, cloning, scanning, and removing repositories update manual
  order without disturbing the relative order of existing entries.
- Provide an accessible non-pointer reordering path for keyboard users in
  Manual mode in addition to drag-and-drop.
- Keep the behavior consistent on macOS Apple Silicon and Windows x64.

### Acceptance criteria

- The repository list can be sorted by age, name, or latest `HEAD` commit in
  either direction.
- Manual mode supports visible drag-and-drop reordering and an accessible
  keyboard equivalent.
- Manual order and the selected automatic sort survive application restarts.
- Opening or refreshing a repository does not silently overwrite its saved
  manual position or original added date.
- Repositories with no commits sort predictably and do not break the repository
  list.
- Scanning a previously known repository does not create duplicates or reset its
  ordering metadata.

## Repository history experience

**Problem:** The History tab is currently a read-only list of at most 30 commits
showing only a subject, abbreviated hash, relative timestamp, author, and
initials. Commits cannot be selected, searched, inspected, copied, or compared,
so the view is not yet useful as a serious repository-history browser.

### Work

- Redesign History as a commit list plus a selected-commit detail view while
  keeping it visually consistent with the Changes workspace.
- Group or label commits with clear absolute dates while retaining concise
  relative time where useful.
- Make each commit selectable and show:
  - Full subject and body
  - Full and abbreviated commit hashes
  - Author and committer identity and timestamps
  - Parent commit hashes
  - Local branch, remote branch, and tag decorations when available
- Show files changed by the selected commit, their added/modified/deleted state,
  and useful addition/deletion totals.
- Let the user select a changed file and inspect its diff for that commit. Define
  merge-commit comparison behavior clearly, using the first parent by default
  unless a parent selector is added. Handle root commits without assuming a
  parent exists.
- Add local history search/filtering by commit message, author, email, or hash.
- Replace the fixed 30-commit ceiling with progressive batch loading. Load an
  initial small batch, request the next batch as the user approaches the end of
  the list, and continue until every reachable commit in the current branch
  history is available. There must be no artificial final commit limit.
- Show distinct initial-loading, loading-more, retry, and end-of-history states.
  Prevent overlapping requests and deduplicate commits at batch boundaries.
- Keep progressive loading stable when the repository changes during viewing:
  refresh from the current `HEAD` when necessary instead of silently skipping or
  repeating commits because earlier pages shifted.
- Add actions to copy the full commit hash and, when `origin` is a recognizable
  GitHub remote, open that commit on GitHub through the existing HTTPS-only
  external-navigation boundary.
- Preserve selection sensibly when more history is loaded or the repository is
  refreshed. Reset it safely when switching repositories.
- Add keyboard navigation for the commit and file lists, visible focus states,
  accessible labels, and clear loading, empty, and error states.
- Fetch commit details and diffs on demand through narrow, validated IPC methods
  rather than embedding an entire repository history in the initial repository
  payload.
- Continue executing Git with argument arrays and `--` separators. Treat commit
  hashes and file paths as untrusted IPC input and verify that requested objects
  belong to the currently opened repository.
- Use only the existing flat SVG icon system; do not introduce emoji or
  low-resolution bitmap controls.

### Acceptance criteria

- History loads progressively as the user scrolls, and continuing to scroll can
  eventually reach every commit in the current branch history without a long
  synchronous UI freeze.
- Repositories with fewer commits stop cleanly at the end, while large
  repositories show visible loading-more progress and never stop at an
  arbitrary cap.
- Selecting a commit shows its complete metadata and changed-file summary.
- Selecting a file shows the correct diff for a normal commit, a root commit,
  and the defined parent of a merge commit.
- Search finds matching commits by message, author, email, and full or partial
  hash within the loaded results.
- Copy-hash and GitHub-open actions work when applicable and are hidden or
  disabled when they are not applicable.
- The History tab is fully usable with keyboard navigation and assistive
  technology.
- Switching repositories cannot leave details or diffs from the previous
  repository visible.

## GitHub OAuth copy feedback and automatic browser launch

**Status:** Implemented in the local source. Keep this item open until the
packaged macOS Apple Silicon and Windows x64 flows satisfy every acceptance
criterion below.

**Original problem:** The one-time device-code button always says `Click to copy`, even
after the clipboard write succeeds, and clipboard failures are ignored. Relay
also relies on `gh auth login --web` to launch the browser. The packaged flow has
been reported to remain inside Relay until the user manually clicks **Open the
GitHub device page again**.

### Work

- Await the clipboard write when the one-time code is clicked instead of
  discarding its promise.
- On success, temporarily replace `Click to copy` with an unmistakable `Copied`
  state and a flat check icon. Announce the success through an `aria-live`
  region without moving focus.
- Reset copy feedback when a new device code arrives, the modal closes, or the
  short confirmation period expires.
- If the clipboard API fails, keep the code visible and selectable and show an
  actionable message telling the user to copy it manually. Never claim success
  before the write resolves.
- After the user starts a login and GitHub CLI emits a device code, explicitly
  open `https://github.com/login/device` in the system browser through the
  Electron main process. Do not rely exclusively on GitHub CLI's browser-launch
  behavior.
- Open the browser only once per login attempt. Repeated progress output,
  re-renders, or copy clicks must not open duplicate tabs.
- Keep **Open the GitHub device page again** as a manual fallback when automatic
  launch is blocked, delayed, or accidentally closed.
- Treat browser-launch failure as recoverable: keep the OAuth attempt running,
  retain the code, expose the fallback button, and show a useful status message.
- Restrict this automatic route to GitHub's exact HTTPS device-login URL; do not
  turn login progress text into an arbitrary external-navigation request.
- Verify the complete behavior in packaged macOS Apple Silicon and Windows x64
  builds, including their default-browser handling.

### Acceptance criteria

- Clicking the device code changes the visible state to `Copied` only after a
  successful clipboard write, and assistive technology announces it.
- A clipboard error leaves the code available for manual selection and never
  displays a false success state.
- Starting GitHub login automatically opens exactly one system-browser tab on
  the official GitHub device page as soon as the one-time code is ready.
- The manual browser button remains usable throughout the pending login.
- Closing and starting a new login resets both the one-shot browser guard and
  the copy-confirmation state.

## Improve the default typography scale

**Problem:** Several labels, metadata rows, badges, form hints, and status-bar
elements use 7px-10px text. This is uncomfortably small on 1440p and 4K displays,
particularly when the operating system is using a low display-scaling setting.
Increase the default text scale modestly across Relay while preserving its
compact desktop layout and information hierarchy.

### Work

- Audit every typography rule in `app/globals.css`, prioritizing text that users
  must read or interact with rather than enlarging only headings.
- Establish a small set of reusable typography tokens instead of continuing to
  assign unrelated one-off pixel sizes throughout the stylesheet.
- Raise ordinary body text, buttons, inputs, tabs, repository/file names,
  history metadata, modal copy, and status text enough to be comfortably
  readable. Aim around 13px for primary control/body text and 11px-12px for
  secondary metadata; reserve smaller sizes only for genuinely nonessential
  compact badges.
- Increase line-height and spacing where needed so larger text does not look
  cramped.
- Preserve a clear distinction between primary labels and secondary metadata.
  Do not make every text element the same size or weight.
- Recheck truncation, wrapping, modal height, toolbar width, repository rows,
  file rows, commit controls, and the Windows application menu after the size
  changes.
- Keep the existing minimum window size usable and avoid solving the issue with
  a hard-coded global browser zoom that would scale icons, borders, and layout
  indiscriminately.
- Verify the packaged app at 2560x1440 and 3840x2160 using representative 100%,
  125%, 150%, and platform-default scaling where available.
- Check both macOS Apple Silicon and Windows x64 because font rendering and DPI
  scaling differ between them.

### Acceptance criteria

- Important text and interactive labels are comfortably readable on both 1440p
  and 4K displays without requiring application-specific zoom.
- No essential action, field label, repository name, file name, commit
  metadata, or status information relies on 7px-9px text.
- The larger typography does not clip controls, overlap icons, hide focus
  outlines, or make primary desktop workflows require horizontal scrolling.
- Visual hierarchy remains clear, with secondary information quieter but still
  readable.
- The no-repository state, repository workspace, History tab, account popover,
  and every modal have been visually checked at the supported window sizes.

## Consolidate Windows chrome and repository headers

**Problem:** Windows currently shows two stacked native chrome rows—the system
title bar containing the Relay name and window controls, followed by the native
File/Edit/View/Window menu. Relay then adds two more application rows: Current
repository in the renderer title bar and Current branch in a separate toolbar.
The result is four header-like rows and unnecessary vertical repetition.

### Work

- On Windows, place the Relay app title and File/Edit/View/Window menus on one
  horizontal chrome row, while keeping minimize, maximize/restore, and close on
  the right.
- Choose an Electron-supported custom-title-bar or title-bar-overlay approach
  that retains ordinary Windows behavior. Preserve window dragging,
  double-click maximize/restore, Aero Snap, DPI scaling, and the system window
  controls.
- Preserve full menu behavior while combining the row: mouse access, Alt-key
  access, keyboard navigation, mnemonics, accelerators, disabled states, and
  screen-reader labels must continue to work.
- Keep the macOS system menu bar native; do not reproduce Windows-specific menu
  chrome inside the macOS window.
- Inspect the packaged Apple Silicon app before treating this as Windows-only.
  Determine whether macOS also has excessive vertical chrome from the hidden
  inset title area, Relay's renderer title bar, and the separate repository
  toolbar, and apply the same in-window consolidation where it improves the
  layout without fighting native macOS conventions.
- On macOS, verify traffic-light placement, title-bar drag regions, double-click
  behavior, fullscreen entry/exit, and spacing around the notch/safe title-bar
  area after any equivalent consolidation.
- Merge Relay's Current repository and Current branch rows into one repository
  action row.
- Arrange the left side of that row in this exact order:
  1. Current repository
  2. Current branch
  3. Fetch origin, Push origin, or Publish branch as appropriate
- Keep the remote-sync control immediately after Current branch. It must be the
  rightmost of those three left-aligned controls, not pushed to the far right by
  a flexible spacer.
- Keep the account switcher on the right side of the merged repository row.
- Remove the `Using @account` identity pill from the toolbar. It duplicates the
  account information already shown elsewhere and should not be moved to a new
  location merely to preserve it.
- Preserve repository-account routing and pinned-account behavior internally;
  removing the duplicate pill is a presentation change, not removal of account
  selection or per-repository bindings.
- Define sensible truncation and minimum widths for long repository, owner,
  branch, and account names so the combined row remains usable at Relay's
  minimum window width.
- Revisit draggable and non-draggable regions after merging rows so buttons,
  selects, menus, and account popovers remain clickable.
- Use flat SVG icons and the updated readable typography scale in the merged
  rows.

### Acceptance criteria

- On packaged Windows x64, the Relay title and File/Edit/View/Window menus share
  one row with the native-equivalent window controls.
- There is one repository action row, ordered Current repository → Current
  branch → remote sync on the left, with the account switcher on the right.
- No `Using @account` pill appears anywhere in the merged header.
- All native menu commands and shortcuts still invoke their existing actions,
  including File-menu repository operations.
- The window can still be dragged, snapped, minimized, maximized/restored, and
  closed using normal Windows conventions.
- The merged rows do not overlap or clip at 100%, 125%, and 150% Windows display
  scaling or at the minimum supported window width.
- The packaged macOS Apple Silicon app has been visually audited for the same
  duplicate-header problem, not assumed correct from the hosted preview or
  Windows result.
- macOS retains its native system menu while using the consolidated repository
  action row inside the app window when the audit confirms the same issue.
- Any macOS adjustment preserves traffic lights, dragging, fullscreen behavior,
  and native title-bar spacing.
