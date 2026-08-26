# Relay Native: plan for replacing the Electron client with C++

Status: **proposed, not started.** Nothing in this document is implemented.

This plan supersedes the "Replace Electron with a native C++ client" entry in
`TODO.md`, which was a sketch written before the decisions below were taken.

---

## 1. Why, honestly

The driver is **footprint and memory**. Measured on this machine, Relay 0.5.0:

| | Measured |
| --- | --- |
| Packaged macOS app | **382 MB** (was 490 MB before the Git payload trim) |
| Of which Electron and Chromium | **287 MB** |
| Of which Relay's own code | **17 MB** |
| Idle resident memory, no repository open | **233 MB across 4 processes** |

Relay is a dense, mostly-native desktop tool that pays for a browser engine it
barely uses. That is the case for the rewrite.

### What this plan is not

Two things are recorded plainly so nobody reads this later and misunderstands
the reasoning.

**C++ is a fixed constraint, not a conclusion.** Tauri was raised and rejected
on preference, not on merit. It would have kept `app/page.tsx` and
`app/globals.css` essentially verbatim — every feature shipped in 0.5.0,
including the history browser, ordering, and SSH identities — and swapped only
the Electron shell, reaching roughly 15 MB in weeks rather than months. The
owner does not want a Rust rewrite. That is a legitimate call for a personal
project, but the plan should not pretend the toolkit was chosen on numbers.

**There is no measured performance problem beyond memory.** Cold start, diff
rendering, and history loading were not raised as complaints. Memory is the
target; everything else is a nice-to-have.

**This delivers no new user value.** Every feature this replaces already works.
The entire project is a cost paid for a smaller, lighter client.

---

## 2. Decisions taken

| Decision | Choice | Consequence |
| --- | --- | --- |
| Language | C++20 | Fixed constraint |
| Toolkit | **Qt 6 Widgets, statically linked** | ~20–35 MB, versus ~95–115 MB for LGPL dynamic |
| Licence | **GPLv3** | Required to link Qt statically without a commercial licence |
| Scope | **Full parity with 0.5.0** | Months of sustained work; see section 4 |
| GitHub auth | **Native OAuth device flow** | Drops the 38 MB `gh` dependency, and moves token custody into Relay |
| Git | **Child-process `git`, bundled** | Preserves the current semantics exactly; ~40 MB |
| Windows builds | **Local Windows machine** | No cross-compilation; both platforms built and smoke-tested natively |
| Electron client | **Frozen at 0.5.0, no further releases** | Parity target stops moving |

### Size and memory targets

These are the numbers the project is judged against. If it misses them badly,
the rewrite has failed at its only stated purpose.

| | Electron 0.5.0 | Native target |
| --- | --- | --- |
| Installed app | 382 MB | **under 100 MB**, ideally 60–80 MB with Git bundled |
| Binary excluding bundled Git | ~342 MB | **under 35 MB** |
| Idle resident memory | 233 MB | **under 80 MB** |
| Processes | 4 | 1 |

---

## 3. Licensing, which must be settled before any code

The repository is public and currently has **no licence at all** — no `LICENSE`
file, and `package.json` declares none. By default that is "all rights
reserved", which is incompatible with the chosen static-Qt path.

Before writing code:

- Add `LICENSE` containing GPLv3, and declare it in the project metadata.
- Understand what this means: anyone distributing a modified Relay binary must
  publish their corresponding source. Relay is already public, so this mostly
  formalises the status quo, but it is a deliberate act and is irreversible in
  practice for code others contribute afterwards.
- Confirm every existing contributor is content for their commits to be
  relicensed. The history currently contains work committed by the owner and by
  two AI assistants under the owner's authorship.
- Building Qt statically means building Qt from source with `-static`. Budget
  hours of compile time per platform, and pin the exact Qt version and
  configure flags in the repository so the build is reproducible.

If GPLv3 turns out to be unacceptable, the fallback is Qt LGPL dynamically
linked at roughly 95–115 MB, which still meets the "under 100 MB" target only
marginally. **Do not start until this is settled.**

---

## 4. What full parity actually means

Ported from 4,834 lines across the renderer and main process, plus 2,248 lines
of Electron service modules, and **33 IPC channels**.

**Accounts and authentication**
- Multiple GitHub.com accounts, browser OAuth, no personal access token
- Switch the active account; per-repository account binding
- Per-account commit email, chosen when connecting, with GitHub's verified
  addresses offered and the noreply address always available
- GitHub profile pictures, cached on disk, with initials as fallback

**Repositories**
- Open, clone, recursive folder scan, remove without touching disk
- Clone browser listing only repositories the account can push to
- Sidebar ordering: manual with drag-and-drop and a keyboard equivalent, by
  age (first commit), by name, by latest commit
- No repository open at startup

**Working tree**
- Changes list with add/modify/delete state and line counts
- Diff viewer
- File-selective commit using the resolved account identity
- Fetch, push, publish branch, switch branch

**History**
- Progressive loading with no cap, anchored paging
- Commit detail: full and short hash, body, author and committer, parents,
  branch and tag decorations
- Changed files per commit and per-file diffs, first-parent for merges,
  empty-tree for root commits
- Search by message, author, email, hash; copy hash; open on GitHub

**Non-GitHub hosts**
- SSH identities with per-repository binding and a connection test
- No key material stored

**Chrome**
- Native menus with accelerators on both platforms
- Single repository action row; macOS traffic-light strip; Windows title row

**The parts most likely to be underestimated**
- The **diff view**. A widget per line will not survive a large diff; this needs
  a custom-painted or model-backed view and is the single hardest piece.
- **Progressive history** with stable anchored paging and no duplicates.
- **Drag-and-drop reordering** with an accessible keyboard path.
- The **Windows in-window menu bar**, which in Electron is Relay's own HTML.
  Qt gets a real `QMenuBar`, so this may become simpler rather than harder.

---

## 5. Security: what changes, and what must not

Relay's current invariants assume a renderer that cannot reach Node.js or a
token. A single-process C++ application has no such boundary, so several
invariants change meaning and must be re-expressed rather than quietly dropped.

**The significant change: Relay starts holding tokens.**

Today the GitHub CLI owns OAuth and stores credentials in the operating system
credential store; Relay never persists a token. Implementing the device flow
natively removes 38 MB and an external dependency, but makes Relay responsible
for token custody. This is the highest-risk part of the rewrite.

Required:
- Tokens go in the **macOS Keychain** (Security framework) and the **Windows
  Credential Manager**, never in `relay-data.json`, never in a file Relay
  writes itself, never in a log or an error message.
- The device flow needs an OAuth App **client ID**, which is public and may be
  embedded. There is no client secret in the device flow; if a design appears
  to need one, that design is wrong.
- One account's token must never be usable for another account's operation.
- The existing rules survive unchanged: no token in a remote URL, in Git
  configuration, in command history, or in release metadata; a token is only
  attached to an HTTPS `github.com` remote; SSH identities are only attached to
  SSH remotes; a GitHub token never reaches another host.
- `GIT_TERMINAL_PROMPT=0` and SSH `BatchMode` stay, so nothing can hang on an
  invisible prompt.
- Git keeps being executed with argument arrays and `--` separators, never a
  command string. `QProcess::setArguments` satisfies this.
- Commit hashes and file paths remain untrusted input: shape-checked, and
  verified to belong to the open repository.

`AGENTS.md` section 2 must be rewritten for the native architecture, with each
of the twelve invariants either restated, re-expressed, or explicitly retired
with a reason.

---

## 6. Sequence

Ordered so the risky and unknown parts come first, and so there is something
runnable early.

**Phase 0 — decide and prepare**
Licence in place. Qt built statically and pinned on both platforms. CMake
project producing a running empty window on macOS and Windows. No product code.
*Exit: `cmake --build` produces a window on both platforms from a documented command.*

**Phase 1 — prove the hard parts**
Two spikes, before committing to the whole rewrite:
1. A diff view rendering a large real diff without freezing.
2. The OAuth device flow end to end, with the token in the OS keychain.

*Exit: both work, or the plan is revisited. This phase exists to fail cheaply.*

**Phase 2 — the service layer**
Port the parsers and Git semantics, which are already specified and already
tested: status, diff, history paging, commit detail, ahead/behind, repository
discovery, ordering rules, SSH command construction. Headless, no UI, with unit
tests ported alongside.
*Exit: the ported logic passes tests equivalent to the current 24.*

**Phase 3 — the shell**
Window, native menus, repository action row, sidebar, tabs.
*Exit: repositories can be opened, scanned, listed and ordered.*

**Phase 4 — the working tree**
Changes list, diff viewer, commit box, fetch and push.
*Exit: a real commit can be made and pushed.*

**Phase 5 — history and accounts**
Progressive history, commit detail, per-file diffs, search. Multi-account
switching, per-repository binding, commit email, avatars.

**Phase 6 — the rest of parity**
Clone browser with push filtering, SSH identities, ordering polish.

**Phase 7 — packaging**
`macdeployqt` and a DMG; Windows installer. Measure against section 2's targets
and record the results.

---

## 7. Testing, which gets worse before it gets better

Relay currently has 24 tests. A meaningful number run **real Git against real
fixture repositories** — history paging, root and merge commits, hash
validation, SSH clone/fetch/push. Those port well and should port early; they
are the most valuable thing the Electron project produced.

Two things are lost outright and need replacing:

- The **server-rendered UI tests**, which depend on the Vinext path.
- The **browser harness** used throughout 0.5.0 development to drive the real
  UI and verify drag-and-drop, keyboard navigation, sort orders, and avatar
  fallback. There is no equivalent; Qt Test plus manual checking is slower and
  catches less. Expect UI regressions the Electron project would have caught.

---

## 8. Risks

| Risk | Reality |
| --- | --- |
| **It stalls half-finished** | Highest risk by far. Electron Relay is frozen at 0.5.0, so a stall leaves users on a client nobody is improving. Phase 1 exists to fail early instead. |
| Static Qt turns out to be painful | Building Qt from source is slow and fiddly, and pinning it across two platforms is ongoing work. |
| GPLv3 is regretted later | Relicensing after third-party contributions is impractical. Settle it in Phase 0. |
| Token custody bug | Relay has never held a token. This is new, security-critical, and deserves outside review. |
| Diff view performance | The one place a naive Qt implementation will be clearly worse than Chromium. |
| Qt does not look native | The existing complaint is that Windows looks worse than macOS. Qt does not fix that automatically and can make macOS look less native too. |
| Scope | Full parity was chosen over a reduced core. That is the long path, by choice. |

---

## 9. Open questions

- Which Qt version, and where is the pinned static build kept?
- Which module owns the GitHub REST calls, and does it use `QNetworkAccessManager`?
- Is `relay-data.json` kept byte-compatible so a user can move between clients
  during the transition, or is a one-way migration acceptable?
- Does the native client keep the name and app ID, or ship alongside?
- Syntax highlighting in the diff view: in scope, or explicitly out?
