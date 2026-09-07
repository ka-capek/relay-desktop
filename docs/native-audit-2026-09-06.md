# Native client audit — 2026-09-06

This is the historical first-pass audit. The subsequent implementation and
five independent reviews supersede its outstanding-work list; see
[the continuation review](native-review-2026-09-06.md).

Reviewed baseline: `9acdfe2` on `main`. The remote `native-qt-successor`
branch has the same tree at the time of this review. No additional rewrite was
found on that branch.

## Product direction

Continue the existing C++26 / dynamically linked Qt Widgets implementation for
macOS Apple Silicon and Windows x64. Multiple GitHub accounts, repository
bindings, and correct commit identities are the central product distinction.
After native parity, add a graphical history of branches and merges inspired
by the interaction the owner remembers from GitKraken.

## What exists

- Roughly 6,900 lines of native implementation across 21 `.cpp` files.
- A real Qt application, native menus, repository sidebar, Changes/History
  views, model-backed diff rendering, and account/clone/SSH dialogs.
- Git status, file-selective commits, fetch, push, local-branch switching,
  progressive history and commit details, repository discovery and ordering.
- Isolated `gh` authentication, per-repository account resolution, commit email
  selection, GitHub repository listing, avatar cache, and compatible JSON state.
- CMake presets for both target platforms, packaging/stage verification tools,
  and 18 CTest entries including the application smoke test.

Source presence is not evidence that all packaged user flows work. The original
plan's “not started” status was stale; the rewrite is substantial but unfinished.

## Changes made during this review

1. The clone dialog clears the previous account's repository selection before
   loading the next account. A generation guard drops superseded lookup results
   and failures, preventing a late response from replacing the current list.
2. The controller unwraps QtConcurrent's `QUnhandledException`. Previously a
   standard service exception reached the UI as `std::exception`, hiding its
   actionable message.
3. Replaced one `QHash::tryInsert` call with equivalent first-occurrence-preserving
   `contains`/`insert` calls, allowing the local diagnostic build with Qt 6.8.2.
   The production dependency pin remains Qt 6.11.1.
4. Added regression coverage for account changes in the clone dialog, late
   account lookup failures, and real file-selective Git commits using a bound
   account followed by fallback to the active account.

## Verification

The source builds with Clang 19.1.7 in C++26 mode, CMake 3.31.6, Ninja 1.12.1,
and dynamically linked Debian Qt 6.8.2 on Linux x86-64. All **18/18 CTest
entries pass**, including real Git fixtures, controller tests, dialog tests,
the main-window integration test, and `app_smoke`.

This is supplemental Linux verification. It does **not** validate Qt 6.11.1,
macOS/Windows packaging, live OAuth, private GitHub transport, OS credential
stores, native screen readers, or target-platform window behavior.

The local toolchain was unpacked into `/tmp/relay-toolchain`, without a system
installation. Build files are ignored under `build-native/linux-audit`.
Reproduction with an equivalent Linux toolchain:

```sh
cmake -S . -B build-native/linux-audit -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-19 \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF \
  -DCMAKE_BUILD_TYPE=Debug \
  -DRELAY_QT_MIN_VERSION=6.8.2
cmake --build build-native/linux-audit --parallel 4
QT_QPA_PLATFORM=offscreen LANG=en_US.UTF-8 LC_ALL=en_US.UTF-8 \
  ctest --test-dir build-native/linux-audit --output-on-failure --timeout 40
```

For a relocated toolchain, also set its compiler sysroot, CMake prefix,
runtime library path and Qt plugin path. Numeric collation tests require a
non-C locale. No live credentials were used by the new tests.

## Remaining work, in order

1. **Finish native behavior and account correctness.** The Edit-menu actions in
   `MainWindow::buildMenus` have shortcuts but no handlers. Account repository
   lookups now have freshness guards; other asynchronous flows still need
   review. `busyOperations_` is a set, so overlapping operations with the same
   name can report idle too early. Clone currently remembers an SSH profile
   but does not retain the chosen GitHub account as a repository binding.
   Define and test that behavior before release.
2. **Harden errors and cancellation.** `GitHubAuth::authenticatedAccounts`
   converts process failures into parsed output, which can turn an unavailable
   CLI into an empty account list. Synchronization can then remove metadata
   bindings. The login loop has no controller cancellation; the synchronous
   process runner applies its output limit after QProcess has buffered output.
   These need targeted fixes and failure-path tests.
3. **Verify the actual targets.** Configure, build and test pinned Qt 6.11.1 on
   macOS arm64 and Windows x64. Prepare reproducible Git/gh runtime inputs,
   produce staged packages, and exercise login with two accounts, private clone,
   commit identity, fetch and push. Record installed/download size and memory
   for the scenarios in `native-measurements.md`. Existing packaging scripts
   and a historical macOS development measurement do not close this gate.
4. **Add the branch graph.** Current History is a list and the branch control
   only lists/switches local branches. Reuse commit IDs and parent IDs, but add
   a topologically ordered history across refs, a deterministic lane-layout
   model, branch/tag decorations, and a virtualized graph beside commit rows.
   Cover merges, remote-only branches, detached HEAD, pagination and long
   histories with real Git fixtures. This feature is planned, not implemented.

The project can continue from this native foundation. It does not need another
rewrite from scratch, and it is not yet ready to replace the released client.
