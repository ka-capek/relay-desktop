# Relay Native: plan for replacing the Electron client with C++

Status (2026-09-07): **native implementation exists; release verification is
unfinished.** `native/` contains the C++26/Qt application, services, UI, tests,
and packaging scripts. This document retains the original design rationale;
its historical estimates and unchecked tasks are not a current implementation
inventory. See [the current implementation report](docs/native-completion-2026-09-07.md).

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
| Language | **C++26** | Fixed constraint; requested by the owner before implementation began |
| Toolkit | **Qt 6 Widgets, dynamically linked** | Forced by the licence choice; see §3 |
| Licence | **MIT for Relay's own code** | Owner wants permissive. This rules out GPLv3, and therefore rules out the easy static-Qt route |
| Scope | **Full parity with 0.5.0** | Months of sustained work; see section 4 |
| GitHub auth | **Keep `gh` for the first release** | Reversed after review; see §5 |
| Git | **Child-process `git`, bundled** | Preserves the current semantics exactly; ~40 MB |
| Windows builds | **Local Windows machine** | No cross-compilation; both platforms built and smoke-tested natively |
| Electron client | **Feature-frozen, security-maintained** | Revised: freezing scope stops the parity target moving, but Chromium and dependency security fixes must continue until the native client ships |

### 2.1 Size and memory targets, and why they are not yet fixed

An external review of the first draft found that the Qt figures here were
invented constants presented as measurements. They have been replaced with
planning ranges, and the decision they were supposed to drive is deferred until
Phase 1 measures a real packaged build.

Defensible planning ranges for Qt Core/Gui/Widgets/Network/Svg, excluding Git
and debug symbols. Ranges, not promises: plugin selection, TLS backend and
translations move them substantially.

| Deployment | Range |
| --- | --- |
| Static, stripped, LTO | ~20–45 MB |
| Dynamic, macOS bundle | ~30–55 MB |
| Dynamic, Windows | ~35–70 MB |

The earlier claim that dynamic LGPL lands at 95–115 MB was wrong; that is an
SDK-sized or indiscriminately deployed tree, not a pruned Widgets deployment.
**Dynamic linking is therefore a live option**, which removes the licensing
pressure the first draft manufactured.

Baseline, measured on this machine, macOS 15 / Apple Silicon:

| | Electron 0.5.0 |
| --- | --- |
| Installed `.app` | 382 MB after the Git trim; 490 MB before it |
| macOS DMG (download) | 197 MB |
| Idle resident, summed RSS across 4 processes | 233 MB |

These carry caveats the first draft omitted:

- "Relay's own code: 17 MB" was wrong. That is `app.asar`, which includes
  packaged dependencies, not Relay source.
- Summed RSS across Chromium processes double-counts shared pages and is not
  directly comparable to one process's RSS.
- No Windows installed-size or memory baseline exists at all.
- Only an idle, no-repository state was measured.

**Before any target is committed, Phase 1 must produce**: installed and download
size per platform; memory in idle, repository-open, large-diff and long-history
states; the same metric (physical footprint / private dirty, not summed RSS)
applied to both implementations; and the exact commands recorded so anyone can
reproduce them.

### 2.2 What the decisions actually add up to

Two decisions taken since the first draft — a permissive licence, which forces
dynamic Qt, and retaining `gh` — both cost disk. Adding the parts up, using the
corrected ranges and the measured Git and `gh` payloads:

| Component | macOS | Windows |
| --- | ---: | ---: |
| Qt, dynamic | 30–55 MB | 35–70 MB |
| Relay's own binary | ~5 MB | ~5 MB |
| Bundled Git, trimmed | 40 MB | 97 MB |
| Bundled `gh` | 38 MB | 40 MB |
| **Installed total** | **~115–140 MB** | **~180–210 MB** |
| Electron today | 382 MB | ~528 MB unpacked |

**The earlier "under 100 MB" target is not reachable while Git and `gh` are
bundled.** It should be dropped rather than quietly missed. The realistic disk
outcome is roughly a 3x reduction, not the order of magnitude the first draft
implied. Getting below 100 MB requires making Git and `gh` external
dependencies, which is already a separate item in `TODO.md` and can be done to
the Electron client without any rewrite.

Windows is the worse case because its bundled Git is 97 MB against macOS's
40 MB. Trimming it further needs testing on a Windows machine.

**Disk is not the goal; memory is.** None of the above affects idle memory,
where the case is much stronger: a single Qt process should sit in the tens of
MB against Electron's measured 233 MB across four processes. Provisional target,
to be confirmed by Phase 1: **idle under 80 MB**. One process is a consequence
of the architecture, not a goal in itself — it also concentrates crash and
security blast radius, and Git and SSH child processes exist regardless.

---

## 3. Licensing, and what it forces

**Decision: Relay's own code is MIT.** The owner wants a permissive licence and
does not want to spend time on the question. That decision is cheap to state and
has one expensive consequence, which is the point of this section.

### It settles the static-versus-dynamic question

Qt's open-source licence is LGPLv3. The interaction with a permissive project
licence is:

| Relay's licence | Qt linkage | Workable? |
| --- | --- | --- |
| MIT | **LGPL Qt, dynamically linked** | **Yes. This is the normal, well-trodden path.** |
| MIT | LGPL Qt, statically linked | Legally possible, but every release must ship object files or equivalent so a recipient can relink against their own Qt. Operationally painful for a solo project. |
| MIT | GPL Qt, statically linked | **No.** GPL would govern the combined work, which contradicts a permissive licence. |

So the smallest-binary option is off the table. **Qt is linked dynamically**, and
the earlier draft's static/GPLv3 direction is abandoned — which is fine, because
the review already established that the static-versus-dynamic size gap is far
smaller than the first draft claimed.

MIT rather than Apache-2.0 for simplicity; Apache-2.0 differs mainly in adding an
explicit patent grant, and is a reasonable substitute if that is wanted.

### What still has to be done

- Add `LICENSE` (MIT) and declare it in project metadata. The repository is
  currently public with **no licence at all**, which grants nobody anything.
  Worth doing for the Electron client immediately, independent of the rewrite.
- **Comply with LGPLv3 for Qt itself.** Relay being MIT does not exempt Qt.
  Ship Qt's licence and notices, state that Qt is used under LGPLv3, link
  dynamically, and make Qt's corresponding source available. Do not statically
  link Qt without revisiting this section.
- **Bundled Git carries its own GPLv2 obligations**, unaffected by Relay's
  licence. Git is GPLv2-only and cannot be relabelled. Relay runs it as a
  separate process, which is ordinarily aggregation rather than a derivative
  work, but the installer must still preserve Git's licence and notices and make
  the exact corresponding source for the shipped binaries available. Git for
  Windows bundles many separately licensed components needing a notice
  inventory. The macOS runtime appears to ship a Git Credential Manager `NOTICE`
  but no Git `COPYING`. **This is a defect in the shipping product today, not a
  rewrite problem.** The same applies to the bundled GitHub CLI.
- An IP provenance note. Much of this repository was written by AI assistants
  under the owner's authorship. Adding a `Co-Authored-By` trailer neither
  creates nor transfers copyright, and an AI tool is not a rights holder whose
  consent can be collected. Under a permissive licence this matters far less
  than it would under copyleft, which is a genuine secondary benefit of the
  choice.

None of this is legal advice.

## 3.1 The design end goal

The native client must reproduce the Electron client's visual design, not
reinterpret it. The frozen Electron client is the specification: `app/page.tsx`
for structure and `app/globals.css` for the visual system, at the freeze commit.

What that means concretely, since an agent will otherwise drift:

- **One repository action row**, ordered current repository, current branch,
  then fetch/push/publish, with the account switcher pushed to the right. On
  macOS the traffic lights sit on their own slim strip above it, and the action
  row starts flush left. On Windows the Relay name and the File/Edit/View/Window
  menus share the title row with the native window controls.
- **Sidebar**: repositories heading with an add button, a filter field, the
  ordering control, then the list. Rows are name over owner, with the pinned
  account avatar and a right-aligned change count. No selection bar down the
  left edge; the selected row is marked by background tint alone.
- **Typography tokens**, not ad-hoc sizes: 13px primary, 11–12px secondary
  metadata, 10px reserved for compact badges. The scale exists because 7–10px
  text was unreadable at low OS scaling; do not reintroduce one-off sizes.
- **Flat, code-native SVG icons** in a single icon set. No emoji, no icon
  fonts, no gradients on icons. Panel and modal shadows are fine.
- **Restrained green accents** for action and selection; coral, violet and blue
  only as avatar fallback tones.
- The empty state, the account popover, and every modal are part of the design,
  not afterthoughts.

Note that any screenshot taken from an installed 0.5.0 build predates two
deliberate changes now on `main`: the "Current repository" caption was removed,
and the macOS traffic lights moved off the action row. **The code is the
specification, not a screenshot.**

## 4. What full parity actually means

Corrected after review, which caught a double count. The real figures:

| | Lines |
| --- | ---: |
| `app/page.tsx` | 2,077 |
| `app/globals.css` | 509 |
| `electron/*.cjs`, including main and preload | 2,248 |
| **Total production source** | **4,834** |

The first draft read this as 4,834 *plus* 2,248. It is 4,834 in total, and the
renderer is ~2,586 lines including CSS, not ~4,800. Plus **33 invoke channels**
and 2 event streams.

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

**Reversed after review: `gh` is retained for the first native release.**

The first draft dropped the GitHub CLI and implemented the device flow
natively. The review made the case against it and I accept it:

- **It saves no idle memory**, which is the project's stated goal. `gh` is
  spawned only for login, account discovery, switching and token retrieval; it
  contributes nothing at rest. The saving is ~38 MB of disk and nothing else.
- **It creates Relay's highest-risk subsystem** — long-lived credentials held
  in a memory-unsafe, single-process C++ application that currently never holds
  a token at all.
- **It breaks existing users.** Relay keeps its own `gh` configuration under the
  user-data folder; native auth abandons those accounts.
- **Device flow is the wrong flow.** GitHub recommends authorization code with
  PKCE for native applications and treats device flow as being for constrained
  or headless clients. A desktop GUI is not headless just because its
  predecessor delegated to a CLI.

Concrete failure modes the first draft had not considered: `interval` and
`slow_down` polling behaviour, denial and expiry states, scope drift between
`repo` and `user:email`, organization SAML policy failures that present as
missing repositories, GitHub's per-user/app/scope token limits revoking older
installations, orphaned keychain entries when a metadata write fails, accounts
keyed by handle rather than numeric ID, tokens surfacing in crash dumps,
Windows generic credentials being readable by other user processes, and macOS
Keychain ACLs being bound to a signing identity that Relay does not currently
have because its builds are unsigned.

If native OAuth is wanted later it should be **authorization code with PKCE**,
specified separately, with signing and keychain identity settled first, and
reviewed by someone outside the project.

**A migration hazard that must not be forgotten.** The Electron client drops
every account whose `authSource` is not `github-cli` when it reads the store
(`electron/main.cjs:96`). If a native client ever writes an account with a
different `authSource` into a shared `relay-data.json`, the Electron client
will silently delete it. Store compatibility is therefore a Phase 0 decision,
not an open question.

Everything else in this section stands:

`AGENTS.md` section 2 must be rewritten for the native architecture, with each
of the twelve invariants either restated, re-expressed, or explicitly retired
with a reason.

---

## 6. Sequence

Ordered so the risky and unknown parts come first, and so there is something
runnable early.

**Phase 0 — decide and prepare**
Licence in place. Qt dynamically linked and pinned on both platforms. CMake
project producing a running empty window on macOS and Windows. No product code.
*Exit: `cmake --build` produces a window on both platforms from a documented command.*

**Phase 1 — a packaged vertical slice on both platforms**
Restructured after review. The first draft deferred packaging and measurement
to Phase 7, which left the project's only business question — does the shipped
application actually get smaller and lighter — untested until after the entire
rewrite. That is backwards.

Phase 1 produces a real installer on macOS and Windows containing:

- Qt Widgets, Network and Svg, with the actual platform, image and TLS plugins
- native menus and the intended window chrome
- one asynchronous `QProcess` Git read, with cancellation
- the custom diff view against a large real diff
- `gh` authentication, retained
- the trimmed Git payload
- the dynamically linked build required by Relay's MIT/LGPL licensing decision

*Exit: installed size, download size and memory in idle, repository-open,
large-diff and long-history states are measured on both platforms and recorded.
If the numbers do not beat Electron by a margin that justifies a developer-year,
the plan stops here. This phase exists to fail cheaply.*

**Phase 2 — the service layer**
Port the parsers and Git semantics, which are already specified and already
tested: status, diff, history paging, commit detail, ahead/behind, repository
discovery, ordering rules, SSH command construction. Headless, no UI, with unit
tests ported alongside.
*Exit: the ported parsers pass equivalents of the Git-fixture tests. Note that
only some of the current 24 are service tests; the rest are SSR and source-text
assertions with no native equivalent, so "equivalent to 24" is not a meaningful
bar.*

Before bulk porting, build one full asynchronous vertical slice end to end. The
existing renderer carries request-generation guards, cancellation, focus
refresh and optimistic persistence; porting parsers first does not de-risk
stale results, destroyed widgets or a repository switch mid-read.

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
- The **browser harness** used during 0.5.0 development to drive the real UI and
  verify drag-and-drop, keyboard navigation, sort orders and avatar fallback.
  Correction: that harness was scratch tooling and **was never checked in**, so
  it is not a project asset being lost — but it also means the repository has
  far less UI coverage than "24 tests" suggests. Most of those are SSR or
  source-text assertions. The native client needs real GUI automation built
  deliberately, because there is nothing to port.

---

## 8. Risks

| Risk | Reality |
| --- | --- |
| **It stalls half-finished** | Highest risk by far. Electron Relay is frozen at 0.5.0, so a stall leaves users on a client nobody is improving. Phase 1 exists to fail early instead. |
| Dynamic Qt deployment turns out to be painful | Pruning and deploying Qt plugins and transitive TLS/image dependencies reproducibly across two platforms is ongoing work. |
| MIT/LGPL compliance drifts | Every package must preserve dynamic relinking, Qt notices and corresponding-source availability. Validate the staged payload in CI. |
| Token custody bug | Relay must continue holding tokens only transiently inside the service/controller layer. A single-process memory-unsafe client makes this boundary especially security-critical. |
| Diff view performance | The one place a naive Qt implementation will be clearly worse than Chromium. |
| Qt does not look native | The existing complaint is that Windows looks worse than macOS. Qt does not fix that automatically and can make macOS look less native too. |
| Scope | Full parity was chosen over a reduced core. That is the long path, by choice. |

### Effort

The first draft said "months of sustained work", which the review correctly
called not an estimate. Assuming one developer already fluent in modern C++, Qt
Widgets, CMake and both platforms; frozen feature scope; child-process Git;
`gh` retained; no auto-update or localization:

| | |
| --- | --- |
| Functional parity | 30–45 engineering weeks |
| Cross-platform hardening and release quality | a further 8–15 weeks |
| **Realistic solo calendar time** | **8–12 months full-time** |

Learning Qt while building, or working part-time, extends this considerably.
The honest framing: this is roughly a developer-year to save ~150 MB of idle
memory on a client that has no other measured performance problem.

---

## 9. Absent from this plan, and from Relay today

The review listed these; they are recorded rather than solved. Each needs to be
labelled parity, regression-prevention, or explicit non-goal before Phase 2.

Accessibility (screen readers, focus order, an accessible representation of a
custom-painted diff) · HiDPI and Windows fractional scaling · internationalization
and `QLocale` parity with the current `Intl` behaviour · crash handling and
child-process cleanup · auto-update, since every bundled Qt or OpenSSL fix still
requires a full rebuild and reship ·
code signing and notarization, which conflicts with "unsigned is out of scope"
if credentials are ever held · persistence schema versioning and corruption
recovery · proxies, corporate TLS roots, timeouts and rate limits · the thread
and cancellation model · SBOM and third-party notices · resource limits for
hostile repositories · redacted diagnostics.

## 10. The case against, which the review made better than the plan did

Recorded because it should not be lost:

> The plan admits the decisive facts itself: Relay has no measured performance
> problem other than memory, the rewrite creates no user value, and every
> existing feature already works.

Spending roughly a developer-year to save ~150 MB of idle memory — while
rewriting working UI, weakening process isolation, and adopting a static
dependency that must be rebuilt for every Qt security fix — is a poor exchange
unless 233 MB is genuinely disqualifying for someone.

The cheaper alternatives, in order: finish the footprint work already in flight
and re-measure; prototype a system-webview shell that keeps the existing UI; and
only then, if C++ remains non-negotiable, proceed — while being clear that it is
a language-preference rewrite rather than an optimisation, developed alongside a
maintained Electron client rather than replacing it up front.

## 11. How to execute this

This plan is intended to be handed to a coding agent, steered by the owner.
Guidance for whoever does that:

- **Phase 1 is a stop/go gate, not a formality.** Its purpose is to kill the
  project cheaply if the measured numbers do not justify the effort. An agent
  will be inclined to push through it. Do not let it.
- **Do not let the agent start with the UI.** The interesting part is the diff
  view and it will want to build that first. Phase 1's vertical slice is the
  right place for it; bulk UI porting is Phase 3 onward.
- **`AGENTS.md` describes the Electron client**, and remains the best
  specification of the behaviour being reproduced — particularly sections 9
  (accounts and credentials), 12 (Git semantics) and 14 (the IPC contract).
  Point the agent at it. It is accurate as of this commit.
- **The Git-fixture tests in `tests/rendered-html.test.mjs` are the most
  valuable artefact the Electron project produced.** Port them early; they
  encode real behaviour for root commits, merges, hash validation and SSH
  transport.
- **Watch for silent scope growth.** Full parity is 33 IPC channels. An agent
  will happily add a 34th.
- Anything in §9 that the agent does not raise, it has not thought about.

## 12. Open questions

Settled since the first draft: toolkit (Qt 6 Widgets), linkage (dynamic, forced
by the licence), licence (MIT), authentication (keep `gh`), and the Electron
client's fate (feature-freeze, keep security fixes).

Still open:

- Which Qt version to pin, and how it is acquired reproducibly on both
  platforms.
- Whether `relay-data.json` stays byte-compatible so a user can move between
  clients during the transition. **Note the hazard in §5**: the Electron client
  deletes any account whose `authSource` is not `github-cli`. Retaining `gh`
  makes compatibility achievable, but it must be decided deliberately.
- Whether the native client keeps the name and app ID, or ships alongside.
- Syntax highlighting in the diff view: in scope, or explicitly out. The
  Electron client does not have it.
- Whether the Electron client gets the "make Git and `gh` external" work from
  `TODO.md` first, which would deliver most of the disk saving without any
  rewrite and would sharpen the comparison.
