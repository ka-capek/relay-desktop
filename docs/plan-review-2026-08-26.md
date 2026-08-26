# External review of PLAN.md — 2026-08-26

Reviewer: Codex (gpt-5.6-sol), asked to be hostile and to disagree rather than
validate. Kept verbatim. `PLAN.md` was revised in response; where the review and
the plan now disagree, the plan records which points were accepted and why.

---


Verdict: I would not approve this plan as written. The rewrite may be defensible as a personal C++ project, but the document currently presents guesses as measurements, makes an incorrect licensing claim, chooses a needlessly risky OAuth architecture, and postpones the only test that matters—whether the shipped application actually meets the size and memory goals—until Phase 7.

## 1. Factual errors

### The Qt size comparison is unsupported and probably wrong

[PLAN.md §2](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:46) says:

> Qt 6 Widgets, statically linked: ~20–35 MB, versus ~95–115 MB for LGPL dynamic.

Those numbers should not drive a licensing or architecture decision.

For this application, using Qt Core, Gui, Widgets, Network and SVG:

| Deployment | Defensible planning range, excluding Git and debug symbols |
| --- | ---: |
| Static, stripped, dead-code elimination/LTO | roughly **20–45 MB** |
| Dynamic macOS bundle | roughly **30–55 MB** |
| Dynamic Windows deployment | roughly **35–70 MB** |

These are ranges, not promises. A feature-reduced static build might come below 20 MB; careless plugin inclusion, TLS dependencies, translations or compiler runtimes can exceed the upper bounds.

As a concrete check, the locally installed ARM64 Qt 6.11.1 library binaries for Core, Gui, Widgets, Network and SVG total approximately 25 MB before adding Relay, the Cocoa platform plugin, style, image-format and TLS plugins. That makes 95–115 MB an implausible minimum for a properly pruned Widgets deployment. It looks more like an SDK-sized or indiscriminately deployed tree.

Conversely, 20–35 MB static is plausible, but not established. Static Qt still needs statically imported platform, image and TLS plugins. Qt explicitly recommends inspecting and pruning the plugin set, and notes that dead-code stripping affects the result. [Qt’s deployment documentation](https://doc.qt.io/qt-6/deployment.html) does not promise either number.

The plan’s conclusion that dynamic LGPL “only marginally” meets 100 MB is therefore unsound. A dynamic build plus the 40 MB macOS Git payload could plausibly land around 70–95 MB. It needs measurement, not arithmetic based on invented constants.

### The current size baseline is not reproducible from the workspace

[PLAN.md §1](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:10) reports a 382 MB macOS application. The only packaged `.app` currently under `outputs/` is 490 MB and contains:

- 287 MB Electron frameworks
- 148 MB Git
- 37 MB `gh`
- 17 MB `app.asar`

The 382 MB figure comes from a later packaging filter in [package.json](/Users/capek/Documents/Codex/2026-08-25/can/package.json:39), not a retained artifact or reproducible measurement report. The claimed 40 MB trimmed Git payload is verified in the commit message for macOS, but not for Windows.

In fact, the Windows filters miss many Git Credential Manager/Avalonia/.NET files under `mingw64/bin`; the source Windows Git tree is 115 MB. Therefore “bundled Git: ~40 MB” in [PLAN.md](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:55) is, at best, a macOS-only result.

Other category problems:

- “Relay’s own code: 17 MB” is false. That is `app.asar`, which includes packaged dependencies and scaffolding, not merely Relay source.
- “Binary excluding bundled Git: 342 MB” is not a binary. It is the whole macOS bundle minus one resource directory.
- Installed size, compressed installer size and download size are mixed conceptually. The current macOS DMG is about 197 MB, not 382 MB.
- No equivalent Windows installed-size or memory baseline is supplied.

### The memory figure is not sufficiently defined

“233 MB resident across four processes” may be accurate for the command used, but the plan never records:

- the exact build and OS version;
- the measurement command;
- whether it is RSS, physical footprint, private dirty memory or proportional set size;
- whether shared Electron pages are counted repeatedly;
- warm-up and settling time;
- account/avatar state;
- working states such as an open repository or large diff.

Summed RSS across Chromium processes is not directly comparable to one process’s RSS. “One process” is also not an engineering quality metric: it increases crash and security blast radius, and Relay will still create Git and SSH child processes during operations.

A reasonable target matrix needs idle, repository-open, 10,000-line diff, long-history and post-fetch states, using the same metric on both implementations.

### The source-size statement double-counts the services

[PLAN.md §4](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:99) claims 4,834 lines “across the renderer and main process, plus 2,248 lines of Electron service modules.”

The actual arithmetic is:

- `app/page.tsx`: 2,077
- `app/globals.css`: 509
- all `electron/*.cjs`, including main and preload: 2,248
- total production source: **4,834**

It is 4,834 total, not 4,834 plus 2,248. The renderer is roughly 2,586 lines including CSS, not 4,800.

The 33 invoke channels are correct. There are also two event streams. Incidentally, [AGENTS.md §14](/Users/capek/Documents/Codex/2026-08-25/can/AGENTS.md:727) is stale: `relay:list-account-emails` and `relay:backfill-repository-metadata` exist in [preload.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/preload.cjs:29) and [main.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/main.cjs:609) but are missing from its IPC table.

### Other factual overstatements

- Tauri would preserve the React UI, but it would not “swap only the Electron shell.” The 2,248-line Node/Electron backend and all 33 invoke routes still need porting or a Node sidecar. The claimed 15 MB and “weeks” in [PLAN.md §1](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:29) are no better substantiated than the Qt figures.
- A real `QMenuBar` does not automatically reproduce Relay’s one-row Windows title bar. The current implementation deliberately hides the native bar and implements keyboard/mnemonic behavior in React; see [application-menu.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/application-menu.cjs:1) and [page.tsx](/Users/capek/Documents/Codex/2026-08-25/can/app/page.tsx:679). Preserving that chrome may still require custom non-client-window work.
- `macdeployqt` is primarily a shared-framework deployment tool. In a static build, Qt plugins must be selected and linked into the executable. [Qt documents that distinction explicitly](https://doc.qt.io/qt-6/macos-deployment.html). Phase 7’s packaging description is therefore muddled.
- The repository contains 24 tests, but there is no checked-in browser automation harness. Most UI coverage is SSR or source-wiring assertions in [rendered-html.test.mjs](/Users/capek/Documents/Codex/2026-08-25/can/tests/rendered-html.test.mjs:90), not end-to-end browser testing.

## 2. The licensing argument

This section contains a central legal error. This is not legal advice, but the plan needs review by someone competent to give it.

### Static Qt does not automatically require GPLv3

[PLAN.md](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:52) says static linking requires GPLv3 absent a commercial licence. That is too categorical.

Static linking can be performed under LGPLv3 if the distributor satisfies the relinking requirements—for example by providing suitable object files, build instructions, the corresponding Qt source and everything necessary for the recipient to relink and run the result. Dynamic linking is much easier, which is why Qt recommends it, but it is not the only LGPL route. [Qt’s own obligations page](https://www.qt.io/development/open-source-lgpl-obligations) describes the relinking and installation-information duties.

Because Relay would publish full source, complying through LGPL static linking may be practical. GPLv3 is still a reasonable deliberate project licence, but it is not forced by static Qt.

### “Public already” is not the status quo

[PLAN.md](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:82) says GPL “mostly formalises the status quo.” It does not.

A publicly visible all-rights-reserved repository generally does not grant broad rights to modify and redistribute. GPLv3 does. Existing GPL grants for distributed versions cannot simply be withdrawn, although the copyright owner may offer future versions under other licences.

The plan also says distributors of modified binaries must “publish” source. More precisely, conveying either modified or unmodified GPL binaries triggers corresponding-source obligations to recipients using one of GPLv3’s permitted mechanisms. Public GitHub publication is one convenient route, not the only one.

### Bundled Git has independent GPLv2 obligations

Git is GPLv2-only, not GPLv3. [The Git project states this explicitly](https://github.com/git/git). Because Relay invokes Git as a separate child process with ordinary arguments and text output, it is likely separate-program aggregation rather than a linked derivative work; the GNU GPL FAQ treats ordinary command-line communication as evidence of separation. [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#MereAggregation).

That does not eliminate Git’s obligations:

- The installer must preserve Git’s GPLv2 licence and notices.
- Recipients need access to the exact corresponding Git source for the shipped binaries, not merely a link to whichever Git source is current.
- Git for Windows contains numerous separately licensed components requiring a proper notice and source inventory.
- Git cannot be relabelled “GPLv3 because Relay is GPLv3.”

The current macOS Git runtime appears to contain a Git Credential Manager `NOTICE` but no obvious Git `COPYING`, while the Windows runtime contains `LICENSE.txt` and component notices. That should be audited now, regardless of the rewrite.

### The AI-contribution statement is not a clearance process

The history has 19 commit messages naming Claude as a co-author, while Git author/committer metadata names the repository owner. There is no visible basis for the statement that “two AI assistants” contributed “under the owner’s authorship.”

An AI tool is not ordinarily a copyright holder whose consent can be collected. The actual questions are:

- Which portions reflect sufficient human authorship?
- Did any human employer, contractor or contributor acquire rights?
- Is any generated output substantially copied from third-party material?
- Do the tool terms grant or assign whatever rights might exist?

The US Copyright Office says AI-generated material is not protected merely because a person prompted the system; protection attaches to qualifying human contributions, selection and modification. [US Copyright Office guidance](https://www.copyright.gov/ai/ai_policy_guidance.pdf). Adding a `Co-Authored-By` trailer neither establishes nor transfers copyright.

The plan needs an IP provenance inventory, not consent from chatbots.

## 3. The OAuth decision

Dropping `gh` is the wrong trade for a rewrite whose stated goal is idle memory.

`gh` consumes 37–40 MB of installed storage but essentially no idle resident memory because it is launched only for authentication, account discovery, switching or token retrieval. The current isolation is compact and understandable in [github-auth.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/github-auth.cjs:8): it owns authentication state, scrubs inherited token variables, selects a named account and retrieves a token only when needed.

Replacing it:

- saves no meaningful idle memory;
- saves only 37–40 MB of disk;
- creates Relay’s highest-risk security subsystem;
- destroys compatibility with users’ existing Relay-specific `gh` accounts;
- places long-lived tokens in a memory-unsafe, single-process C++ UI.

Worse, GitHub currently recommends authorization-code flow with PKCE for native public clients and warns against device flow except for constrained/headless applications because a public client ID is spoofable and device flow can facilitate remote impersonation/phishing. [GitHub OAuth best practices](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/best-practices-for-creating-an-oauth-app). A GUI desktop application is not headless merely because its predecessor delegated to a CLI.

If native OAuth is still chosen, likely failures include:

- Incorrect polling of `interval`, `slow_down`, expiry, denial and cancellation. GitHub specifies these states and rate limits. [GitHub device-flow documentation](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps)
- Wrong scopes. Private fetch/push requires broad `repo`; verified email requires `user:email`; users may reduce scopes later. [GitHub OAuth scopes](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/scopes-for-oauth-apps)
- SAML/organization-policy failures that look like missing repositories.
- Repeated login revoking older installations: GitHub limits tokens per user/application/scope combination.
- Orphaned keychain credentials when metadata writes fail, or dead metadata when credential writes fail.
- Handle changes and duplicate-account mapping unless credentials are keyed by numeric GitHub ID.
- Token refresh/expiration being added later without a schema and rotation design.
- App-registration ownership becoming production infrastructure: suspension, deletion or disabling device flow breaks every login.
- Tokens appearing in crash dumps, diagnostic logs or environment inspection.
- Windows Credential Manager providing weaker isolation than the phrase “OS credential store” suggests. Microsoft documents that generic credentials can be read by user processes and are not protected by Credential Guard. [Microsoft credential documentation](https://learn.microsoft.com/en-us/windows/win32/secauthn/kinds-of-credentials)
- macOS Keychain access changing across unsigned/ad-hoc-signed builds. Keychain access control is tied to application identity and entitlements; proper stable signing is not optional if credentials must survive upgrades predictably. [Apple Keychain ACL documentation](https://developer.apple.com/documentation/security/access-control-lists)

My recommendation is to retain `gh` for the first native release. If footprint later justifies native OAuth, implement authorization code plus PKCE as a separate, security-reviewed migration.

## 4. Sequencing

The diff spike is sensible. The OAuth spike is not the second most important risk.

The largest unde-risked risk is that the actual packaged, signed-or-ad-hoc-signed, cross-platform application will miss its footprint, memory, deployment or UI-integration targets. Yet [Phase 7](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:227) performs packaging and measurement after the entire rewrite.

Phase 1 should instead produce a representative packaged vertical slice on both platforms containing:

- Widgets, Network and SVG;
- the real platform, image and TLS plugins;
- a native menu and intended window chrome;
- one asynchronous `QProcess` Git read;
- the custom diff view;
- retained `gh` authentication;
- the trimmed Git payload;
- installers and measured memory in multiple states.

That spike answers the project’s only business question.

Other sequencing defects:

- Do not add GPLv3 in Phase 0 based on the false claim that static Qt requires it.
- Do not freeze Electron maintenance. Freeze feature scope, but continue critical fixes and Electron/Chromium security updates until native Relay ships.
- Persistence compatibility cannot remain an open Phase-9 question. The old client drops every account whose `authSource` is not `github-cli`; see [main.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/main.cjs:91). A native-auth account written to the shared file would disappear when the Electron client next reads it.
- Phase 2’s “tests equivalent to the current 24” exit is incoherent because many of those 24 tests are SSR or source-text assertions rather than service tests.
- Signing and keychain identity must be decided before the credential spike, not after it.
- A full asynchronous vertical slice should precede bulk porting. The renderer currently has request-generation checks, cancellation guards, focus refresh and optimistic persistence—see [page.tsx](/Users/capek/Documents/Codex/2026-08-25/can/app/page.tsx:723) and [page.tsx](/Users/capek/Documents/Codex/2026-08-25/can/app/page.tsx:1128). Porting parsers first does not de-risk stale results, destroyed widgets or concurrent repository switches.

## 5. Underestimation and realistic effort

The project is underestimated even though the line-count claim overcounts the existing source.

The 33 IPC methods are not 33 simple function calls. They encode validation, persistence, account routing, cancellation, native dialogs, paging and security boundaries. The renderer is a monolithic state machine with many coupled modal, history, repository and busy states. Porting it to Widgets means designing models, ownership, signal lifetimes and asynchronous error paths, not translating JSX.

Particularly underestimated:

- native window chrome and Windows menus;
- accessibility for a custom-painted diff;
- large-list virtualization and selection preservation;
- cancellation and stale-result suppression;
- Unicode Git output and Windows path/console encoding;
- Git credential-helper quoting and secret lifetime;
- atomic store migration and cross-version compatibility;
- platform-specific OAuth/keychain behavior;
- installer construction, signing and TLS plugin deployment;
- rebuilding UI test coverage that currently barely exists.

Assuming one senior developer who is already competent in modern C++, Qt Widgets, CMake, macOS and Windows; frozen feature scope; child-process Git; retained `gh`; and no auto-update or localization:

- Functional parity: **30–45 engineering weeks**
- Cross-platform hardening and release quality: **another 8–15 weeks**
- Realistic solo calendar estimate: **8–12 months full-time**

Native OAuth, serious accessibility, signing/notarization and a secure update mechanism push this toward **10–15 months**. A developer learning Qt or working part-time should expect longer. “Months of sustained work” is not an estimate suitable for deciding whether to begin.

## 6. What is missing entirely

Some items are absent from current Relay too, but a replacement plan still needs to state whether each is parity, regression prevention or explicit non-goal.

- **Accessibility:** VoiceOver and Narrator tests, focus order, dialog focus trapping, accessible table/list models, screen-reader representation of custom-painted diff lines, contrast and keyboard-only acceptance criteria.
- **HiDPI:** Windows fractional scaling, monitor changes at runtime, retina rasterization, SVG stroke behavior, font scaling and persisted window geometry across displays.
- **Internationalization:** English-only decision, translatable strings, `QLocale` date/collation parity with the current `Intl` behavior, Unicode paths and right-to-left implications.
- **Crash handling:** recovery from partial operations, crash-safe state, child-process cleanup, redacted crash dumps and whether crash reporting exists.
- **Updates:** static Qt creates a stronger obligation to ship promptly after Qt/OpenSSL vulnerabilities. There is no auto-update or secure update channel.
- **Signing:** Developer ID/notarization, Windows signing/SmartScreen and stable macOS keychain identity. “Unsigned remains out of scope” conflicts with reliable token custody.
- **Persistence:** schema versioning, corruption recovery, backups, Windows ACLs, side-by-side-client locking, migration rollback and window/settings persistence.
- **Network behavior:** proxy support, TLS backend selection, corporate certificates, timeouts, redirects, cancellation, GitHub rate limits, offline behavior and organization SSO.
- **Concurrency:** thread model, bounded work queues, scan cancellation, repository switching during Git reads and shutdown while `QProcess` is active.
- **Dependency governance:** SBOM, third-party notices, exact corresponding-source archives, vulnerability monitoring and reproducible acquisition of Qt, Git, OpenSSL and installer tooling.
- **Testing:** real GUI automation on both platforms, screen-reader smoke tests, scaling matrix, packaged TLS/OAuth tests, performance fixtures and long-running failure injection.
- **Resource limits:** maximum diff size, output-buffer policy, binary detection and denial-of-service behavior for malicious repositories.
- **Operational logging:** structured but redacted diagnostics, log rotation and a user-accessible support bundle.

## 7. The strongest argument against doing it

The plan admits the decisive facts itself: Relay has no measured performance problem other than memory, the rewrite creates no user value, and every existing feature already works.

The proposed response to a 153 MB idle-memory saving is to spend roughly a developer-year:

- rewriting working UI;
- discarding its current implementation and test strategy;
- moving OAuth token custody into memory-unsafe application code;
- weakening process isolation;
- freezing security maintenance on the shipping client;
- adopting a static dependency that must be rebuilt for every Qt security fix.

That is a poor engineering exchange unless Relay has a user population for whom 233 MB is demonstrably disqualifying.

What I would do instead:

1. Establish reproducible per-platform footprint and memory baselines first. Prune unused locales/dependencies, finish and verify Git trimming on Windows, and consider making bundled Git optional. Keep `gh`; its disk cost does not affect idle memory.
2. If memory remains a product blocker, prototype a system-webview shell that preserves the React/CSS UI. Tauri still requires porting the backend, but it avoids rebuilding the most interaction-heavy 2,586 lines and should be evaluated on measured results rather than rejected on language preference.
3. If C++ remains non-negotiable, call this what it is: a hobby/strategic rewrite motivated by language preference. Develop it side-by-side, retain `gh`, and keep Electron maintained until the native application passes parity and release gates.

## Three changes I would insist on

1. **Replace all size, memory and licensing conclusions with a reproducible benchmark-and-licence decision record.** Include macOS and Windows installed/download sizes, exact measurement commands, static and dynamic Qt prototypes, plugin lists, Git payloads and LGPL-static compliance as a real option.

2. **Remove native device-flow OAuth from the first native release.** Retain `gh`; if native OAuth remains desired, specify authorization code with PKCE, signing/keychain requirements, scopes, token lifecycle, migration and an external security review as a separate project.

3. **Reorder the plan around an early packaged vertical slice and add a real schedule.** Packaging, window chrome, Git/`gh`, diff rendering, TLS, migration and memory measurements must pass on both platforms before the service/UI port begins; budget approximately 8–12 full-time months and continue security maintenance of Electron meanwhile.
tokens used
186 795
# Hostile review of PLAN.md

Verdict: I would not approve this plan as written. The rewrite may be defensible as a personal C++ project, but the document currently presents guesses as measurements, makes an incorrect licensing claim, chooses a needlessly risky OAuth architecture, and postpones the only test that matters—whether the shipped application actually meets the size and memory goals—until Phase 7.

## 1. Factual errors

### The Qt size comparison is unsupported and probably wrong

[PLAN.md §2](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:46) says:

> Qt 6 Widgets, statically linked: ~20–35 MB, versus ~95–115 MB for LGPL dynamic.

Those numbers should not drive a licensing or architecture decision.

For this application, using Qt Core, Gui, Widgets, Network and SVG:

| Deployment | Defensible planning range, excluding Git and debug symbols |
| --- | ---: |
| Static, stripped, dead-code elimination/LTO | roughly **20–45 MB** |
| Dynamic macOS bundle | roughly **30–55 MB** |
| Dynamic Windows deployment | roughly **35–70 MB** |

These are ranges, not promises. A feature-reduced static build might come below 20 MB; careless plugin inclusion, TLS dependencies, translations or compiler runtimes can exceed the upper bounds.

As a concrete check, the locally installed ARM64 Qt 6.11.1 library binaries for Core, Gui, Widgets, Network and SVG total approximately 25 MB before adding Relay, the Cocoa platform plugin, style, image-format and TLS plugins. That makes 95–115 MB an implausible minimum for a properly pruned Widgets deployment. It looks more like an SDK-sized or indiscriminately deployed tree.

Conversely, 20–35 MB static is plausible, but not established. Static Qt still needs statically imported platform, image and TLS plugins. Qt explicitly recommends inspecting and pruning the plugin set, and notes that dead-code stripping affects the result. [Qt’s deployment documentation](https://doc.qt.io/qt-6/deployment.html) does not promise either number.

The plan’s conclusion that dynamic LGPL “only marginally” meets 100 MB is therefore unsound. A dynamic build plus the 40 MB macOS Git payload could plausibly land around 70–95 MB. It needs measurement, not arithmetic based on invented constants.

### The current size baseline is not reproducible from the workspace

[PLAN.md §1](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:10) reports a 382 MB macOS application. The only packaged `.app` currently under `outputs/` is 490 MB and contains:

- 287 MB Electron frameworks
- 148 MB Git
- 37 MB `gh`
- 17 MB `app.asar`

The 382 MB figure comes from a later packaging filter in [package.json](/Users/capek/Documents/Codex/2026-08-25/can/package.json:39), not a retained artifact or reproducible measurement report. The claimed 40 MB trimmed Git payload is verified in the commit message for macOS, but not for Windows.

In fact, the Windows filters miss many Git Credential Manager/Avalonia/.NET files under `mingw64/bin`; the source Windows Git tree is 115 MB. Therefore “bundled Git: ~40 MB” in [PLAN.md](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:55) is, at best, a macOS-only result.

Other category problems:

- “Relay’s own code: 17 MB” is false. That is `app.asar`, which includes packaged dependencies and scaffolding, not merely Relay source.
- “Binary excluding bundled Git: 342 MB” is not a binary. It is the whole macOS bundle minus one resource directory.
- Installed size, compressed installer size and download size are mixed conceptually. The current macOS DMG is about 197 MB, not 382 MB.
- No equivalent Windows installed-size or memory baseline is supplied.

### The memory figure is not sufficiently defined

“233 MB resident across four processes” may be accurate for the command used, but the plan never records:

- the exact build and OS version;
- the measurement command;
- whether it is RSS, physical footprint, private dirty memory or proportional set size;
- whether shared Electron pages are counted repeatedly;
- warm-up and settling time;
- account/avatar state;
- working states such as an open repository or large diff.

Summed RSS across Chromium processes is not directly comparable to one process’s RSS. “One process” is also not an engineering quality metric: it increases crash and security blast radius, and Relay will still create Git and SSH child processes during operations.

A reasonable target matrix needs idle, repository-open, 10,000-line diff, long-history and post-fetch states, using the same metric on both implementations.

### The source-size statement double-counts the services

[PLAN.md §4](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:99) claims 4,834 lines “across the renderer and main process, plus 2,248 lines of Electron service modules.”

The actual arithmetic is:

- `app/page.tsx`: 2,077
- `app/globals.css`: 509
- all `electron/*.cjs`, including main and preload: 2,248
- total production source: **4,834**

It is 4,834 total, not 4,834 plus 2,248. The renderer is roughly 2,586 lines including CSS, not 4,800.

The 33 invoke channels are correct. There are also two event streams. Incidentally, [AGENTS.md §14](/Users/capek/Documents/Codex/2026-08-25/can/AGENTS.md:727) is stale: `relay:list-account-emails` and `relay:backfill-repository-metadata` exist in [preload.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/preload.cjs:29) and [main.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/main.cjs:609) but are missing from its IPC table.

### Other factual overstatements

- Tauri would preserve the React UI, but it would not “swap only the Electron shell.” The 2,248-line Node/Electron backend and all 33 invoke routes still need porting or a Node sidecar. The claimed 15 MB and “weeks” in [PLAN.md §1](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:29) are no better substantiated than the Qt figures.
- A real `QMenuBar` does not automatically reproduce Relay’s one-row Windows title bar. The current implementation deliberately hides the native bar and implements keyboard/mnemonic behavior in React; see [application-menu.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/application-menu.cjs:1) and [page.tsx](/Users/capek/Documents/Codex/2026-08-25/can/app/page.tsx:679). Preserving that chrome may still require custom non-client-window work.
- `macdeployqt` is primarily a shared-framework deployment tool. In a static build, Qt plugins must be selected and linked into the executable. [Qt documents that distinction explicitly](https://doc.qt.io/qt-6/macos-deployment.html). Phase 7’s packaging description is therefore muddled.
- The repository contains 24 tests, but there is no checked-in browser automation harness. Most UI coverage is SSR or source-wiring assertions in [rendered-html.test.mjs](/Users/capek/Documents/Codex/2026-08-25/can/tests/rendered-html.test.mjs:90), not end-to-end browser testing.

## 2. The licensing argument

This section contains a central legal error. This is not legal advice, but the plan needs review by someone competent to give it.

### Static Qt does not automatically require GPLv3

[PLAN.md](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:52) says static linking requires GPLv3 absent a commercial licence. That is too categorical.

Static linking can be performed under LGPLv3 if the distributor satisfies the relinking requirements—for example by providing suitable object files, build instructions, the corresponding Qt source and everything necessary for the recipient to relink and run the result. Dynamic linking is much easier, which is why Qt recommends it, but it is not the only LGPL route. [Qt’s own obligations page](https://www.qt.io/development/open-source-lgpl-obligations) describes the relinking and installation-information duties.

Because Relay would publish full source, complying through LGPL static linking may be practical. GPLv3 is still a reasonable deliberate project licence, but it is not forced by static Qt.

### “Public already” is not the status quo

[PLAN.md](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:82) says GPL “mostly formalises the status quo.” It does not.

A publicly visible all-rights-reserved repository generally does not grant broad rights to modify and redistribute. GPLv3 does. Existing GPL grants for distributed versions cannot simply be withdrawn, although the copyright owner may offer future versions under other licences.

The plan also says distributors of modified binaries must “publish” source. More precisely, conveying either modified or unmodified GPL binaries triggers corresponding-source obligations to recipients using one of GPLv3’s permitted mechanisms. Public GitHub publication is one convenient route, not the only one.

### Bundled Git has independent GPLv2 obligations

Git is GPLv2-only, not GPLv3. [The Git project states this explicitly](https://github.com/git/git). Because Relay invokes Git as a separate child process with ordinary arguments and text output, it is likely separate-program aggregation rather than a linked derivative work; the GNU GPL FAQ treats ordinary command-line communication as evidence of separation. [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html#MereAggregation).

That does not eliminate Git’s obligations:

- The installer must preserve Git’s GPLv2 licence and notices.
- Recipients need access to the exact corresponding Git source for the shipped binaries, not merely a link to whichever Git source is current.
- Git for Windows contains numerous separately licensed components requiring a proper notice and source inventory.
- Git cannot be relabelled “GPLv3 because Relay is GPLv3.”

The current macOS Git runtime appears to contain a Git Credential Manager `NOTICE` but no obvious Git `COPYING`, while the Windows runtime contains `LICENSE.txt` and component notices. That should be audited now, regardless of the rewrite.

### The AI-contribution statement is not a clearance process

The history has 19 commit messages naming Claude as a co-author, while Git author/committer metadata names the repository owner. There is no visible basis for the statement that “two AI assistants” contributed “under the owner’s authorship.”

An AI tool is not ordinarily a copyright holder whose consent can be collected. The actual questions are:

- Which portions reflect sufficient human authorship?
- Did any human employer, contractor or contributor acquire rights?
- Is any generated output substantially copied from third-party material?
- Do the tool terms grant or assign whatever rights might exist?

The US Copyright Office says AI-generated material is not protected merely because a person prompted the system; protection attaches to qualifying human contributions, selection and modification. [US Copyright Office guidance](https://www.copyright.gov/ai/ai_policy_guidance.pdf). Adding a `Co-Authored-By` trailer neither establishes nor transfers copyright.

The plan needs an IP provenance inventory, not consent from chatbots.

## 3. The OAuth decision

Dropping `gh` is the wrong trade for a rewrite whose stated goal is idle memory.

`gh` consumes 37–40 MB of installed storage but essentially no idle resident memory because it is launched only for authentication, account discovery, switching or token retrieval. The current isolation is compact and understandable in [github-auth.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/github-auth.cjs:8): it owns authentication state, scrubs inherited token variables, selects a named account and retrieves a token only when needed.

Replacing it:

- saves no meaningful idle memory;
- saves only 37–40 MB of disk;
- creates Relay’s highest-risk security subsystem;
- destroys compatibility with users’ existing Relay-specific `gh` accounts;
- places long-lived tokens in a memory-unsafe, single-process C++ UI.

Worse, GitHub currently recommends authorization-code flow with PKCE for native public clients and warns against device flow except for constrained/headless applications because a public client ID is spoofable and device flow can facilitate remote impersonation/phishing. [GitHub OAuth best practices](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/best-practices-for-creating-an-oauth-app). A GUI desktop application is not headless merely because its predecessor delegated to a CLI.

If native OAuth is still chosen, likely failures include:

- Incorrect polling of `interval`, `slow_down`, expiry, denial and cancellation. GitHub specifies these states and rate limits. [GitHub device-flow documentation](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/authorizing-oauth-apps)
- Wrong scopes. Private fetch/push requires broad `repo`; verified email requires `user:email`; users may reduce scopes later. [GitHub OAuth scopes](https://docs.github.com/en/apps/oauth-apps/building-oauth-apps/scopes-for-oauth-apps)
- SAML/organization-policy failures that look like missing repositories.
- Repeated login revoking older installations: GitHub limits tokens per user/application/scope combination.
- Orphaned keychain credentials when metadata writes fail, or dead metadata when credential writes fail.
- Handle changes and duplicate-account mapping unless credentials are keyed by numeric GitHub ID.
- Token refresh/expiration being added later without a schema and rotation design.
- App-registration ownership becoming production infrastructure: suspension, deletion or disabling device flow breaks every login.
- Tokens appearing in crash dumps, diagnostic logs or environment inspection.
- Windows Credential Manager providing weaker isolation than the phrase “OS credential store” suggests. Microsoft documents that generic credentials can be read by user processes and are not protected by Credential Guard. [Microsoft credential documentation](https://learn.microsoft.com/en-us/windows/win32/secauthn/kinds-of-credentials)
- macOS Keychain access changing across unsigned/ad-hoc-signed builds. Keychain access control is tied to application identity and entitlements; proper stable signing is not optional if credentials must survive upgrades predictably. [Apple Keychain ACL documentation](https://developer.apple.com/documentation/security/access-control-lists)

My recommendation is to retain `gh` for the first native release. If footprint later justifies native OAuth, implement authorization code plus PKCE as a separate, security-reviewed migration.

## 4. Sequencing

The diff spike is sensible. The OAuth spike is not the second most important risk.

The largest unde-risked risk is that the actual packaged, signed-or-ad-hoc-signed, cross-platform application will miss its footprint, memory, deployment or UI-integration targets. Yet [Phase 7](/Users/capek/Documents/Codex/2026-08-25/can/PLAN.md:227) performs packaging and measurement after the entire rewrite.

Phase 1 should instead produce a representative packaged vertical slice on both platforms containing:

- Widgets, Network and SVG;
- the real platform, image and TLS plugins;
- a native menu and intended window chrome;
- one asynchronous `QProcess` Git read;
- the custom diff view;
- retained `gh` authentication;
- the trimmed Git payload;
- installers and measured memory in multiple states.

That spike answers the project’s only business question.

Other sequencing defects:

- Do not add GPLv3 in Phase 0 based on the false claim that static Qt requires it.
- Do not freeze Electron maintenance. Freeze feature scope, but continue critical fixes and Electron/Chromium security updates until native Relay ships.
- Persistence compatibility cannot remain an open Phase-9 question. The old client drops every account whose `authSource` is not `github-cli`; see [main.cjs](/Users/capek/Documents/Codex/2026-08-25/can/electron/main.cjs:91). A native-auth account written to the shared file would disappear when the Electron client next reads it.
- Phase 2’s “tests equivalent to the current 24” exit is incoherent because many of those 24 tests are SSR or source-text assertions rather than service tests.
- Signing and keychain identity must be decided before the credential spike, not after it.
- A full asynchronous vertical slice should precede bulk porting. The renderer currently has request-generation checks, cancellation guards, focus refresh and optimistic persistence—see [page.tsx](/Users/capek/Documents/Codex/2026-08-25/can/app/page.tsx:723) and [page.tsx](/Users/capek/Documents/Codex/2026-08-25/can/app/page.tsx:1128). Porting parsers first does not de-risk stale results, destroyed widgets or concurrent repository switches.

## 5. Underestimation and realistic effort

The project is underestimated even though the line-count claim overcounts the existing source.

The 33 IPC methods are not 33 simple function calls. They encode validation, persistence, account routing, cancellation, native dialogs, paging and security boundaries. The renderer is a monolithic state machine with many coupled modal, history, repository and busy states. Porting it to Widgets means designing models, ownership, signal lifetimes and asynchronous error paths, not translating JSX.

Particularly underestimated:

- native window chrome and Windows menus;
- accessibility for a custom-painted diff;
- large-list virtualization and selection preservation;
- cancellation and stale-result suppression;
- Unicode Git output and Windows path/console encoding;
- Git credential-helper quoting and secret lifetime;
- atomic store migration and cross-version compatibility;
- platform-specific OAuth/keychain behavior;
- installer construction, signing and TLS plugin deployment;
- rebuilding UI test coverage that currently barely exists.

Assuming one senior developer who is already competent in modern C++, Qt Widgets, CMake, macOS and Windows; frozen feature scope; child-process Git; retained `gh`; and no auto-update or localization:

- Functional parity: **30–45 engineering weeks**
- Cross-platform hardening and release quality: **another 8–15 weeks**
- Realistic solo calendar estimate: **8–12 months full-time**

Native OAuth, serious accessibility, signing/notarization and a secure update mechanism push this toward **10–15 months**. A developer learning Qt or working part-time should expect longer. “Months of sustained work” is not an estimate suitable for deciding whether to begin.

## 6. What is missing entirely

Some items are absent from current Relay too, but a replacement plan still needs to state whether each is parity, regression prevention or explicit non-goal.

- **Accessibility:** VoiceOver and Narrator tests, focus order, dialog focus trapping, accessible table/list models, screen-reader representation of custom-painted diff lines, contrast and keyboard-only acceptance criteria.
- **HiDPI:** Windows fractional scaling, monitor changes at runtime, retina rasterization, SVG stroke behavior, font scaling and persisted window geometry across displays.
- **Internationalization:** English-only decision, translatable strings, `QLocale` date/collation parity with the current `Intl` behavior, Unicode paths and right-to-left implications.
- **Crash handling:** recovery from partial operations, crash-safe state, child-process cleanup, redacted crash dumps and whether crash reporting exists.
- **Updates:** static Qt creates a stronger obligation to ship promptly after Qt/OpenSSL vulnerabilities. There is no auto-update or secure update channel.
- **Signing:** Developer ID/notarization, Windows signing/SmartScreen and stable macOS keychain identity. “Unsigned remains out of scope” conflicts with reliable token custody.
- **Persistence:** schema versioning, corruption recovery, backups, Windows ACLs, side-by-side-client locking, migration rollback and window/settings persistence.
- **Network behavior:** proxy support, TLS backend selection, corporate certificates, timeouts, redirects, cancellation, GitHub rate limits, offline behavior and organization SSO.
- **Concurrency:** thread model, bounded work queues, scan cancellation, repository switching during Git reads and shutdown while `QProcess` is active.
- **Dependency governance:** SBOM, third-party notices, exact corresponding-source archives, vulnerability monitoring and reproducible acquisition of Qt, Git, OpenSSL and installer tooling.
- **Testing:** real GUI automation on both platforms, screen-reader smoke tests, scaling matrix, packaged TLS/OAuth tests, performance fixtures and long-running failure injection.
- **Resource limits:** maximum diff size, output-buffer policy, binary detection and denial-of-service behavior for malicious repositories.
- **Operational logging:** structured but redacted diagnostics, log rotation and a user-accessible support bundle.

## 7. The strongest argument against doing it

The plan admits the decisive facts itself: Relay has no measured performance problem other than memory, the rewrite creates no user value, and every existing feature already works.

The proposed response to a 153 MB idle-memory saving is to spend roughly a developer-year:

- rewriting working UI;
- discarding its current implementation and test strategy;
- moving OAuth token custody into memory-unsafe application code;
- weakening process isolation;
- freezing security maintenance on the shipping client;
- adopting a static dependency that must be rebuilt for every Qt security fix.

That is a poor engineering exchange unless Relay has a user population for whom 233 MB is demonstrably disqualifying.

What I would do instead:

1. Establish reproducible per-platform footprint and memory baselines first. Prune unused locales/dependencies, finish and verify Git trimming on Windows, and consider making bundled Git optional. Keep `gh`; its disk cost does not affect idle memory.
2. If memory remains a product blocker, prototype a system-webview shell that preserves the React/CSS UI. Tauri still requires porting the backend, but it avoids rebuilding the most interaction-heavy 2,586 lines and should be evaluated on measured results rather than rejected on language preference.
3. If C++ remains non-negotiable, call this what it is: a hobby/strategic rewrite motivated by language preference. Develop it side-by-side, retain `gh`, and keep Electron maintained until the native application passes parity and release gates.

## Three changes I would insist on

1. **Replace all size, memory and licensing conclusions with a reproducible benchmark-and-licence decision record.** Include macOS and Windows installed/download sizes, exact measurement commands, static and dynamic Qt prototypes, plugin lists, Git payloads and LGPL-static compliance as a real option.

2. **Remove native device-flow OAuth from the first native release.** Retain `gh`; if native OAuth remains desired, specify authorization code with PKCE, signing/keychain requirements, scopes, token lifecycle, migration and an external security review as a separate project.

