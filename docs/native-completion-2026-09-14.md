# Native completion — 14 September 2026

The native client remains on `codex/native-completion`, with Electron 0.5.0
frozen. This continuation finishes local source-installation tooling and first-run
dependency guidance. It does not publish a replacement release.

## Platform evidence verified

GitHub API reads on 14 September confirm that
[run 34118316989](https://github.com/ka-capek/relay-desktop/actions/runs/34118316989)
completed successfully for `ca05c2bb5c5aa137a78cfdf33476289e5eb18185`:

- Windows x64: pinned Qt 6.11.1 configure, compile, runtime deployment and CTest.
- macOS arm64: pinned Qt 6.11.1 configure, compile, CTest, source installation
  and installed-app launch through Launch Services.

[PR #1](https://github.com/ka-capek/relay-desktop/pull/1) is still open and unmerged.
These results supersede stale statements that the platform jobs have never run.
They apply to that commit only.

## New local work

- Windows source installer with pinned isolated tools, cached official Qt,
  deployed dynamic Qt/MSVC runtime, clean-PATH startup validation, Start menu
  shortcut, dated application backups and rollback on replacement failure.
  It refuses unrelated existing destinations and running Relay processes.
- Startup checks for supported Git/gh versions; missing-tool installation
  guidance stays visible until retry succeeds. Help can repeat the checks.
  Version checks never authenticate or expose process output. Missing gh keeps
  stored accounts intact, and local Git use remains available.
- Standard Windows Git/gh paths are rediscovered after installation, even if
  the running application still has the previous PATH. External Git paths
  retain their own runtime configuration.
- Natural repository sorting works with the C locale in both the service
  ordering and UI model, including `repo2` before `repo10`.
- CI now has a Windows source-installation check and downloadable macOS/Windows
  source-build artifacts. Source-build artifacts require external Git and gh;
  they are distinct from self-contained release installers.

## Validation and remaining release work

Local validation uses Linux x64, Clang 19/C++26, CMake 3.31.6 and Qt 6.8.2 in
an isolated `/tmp` toolchain. Production presets retain the Qt 6.11.1 pin.
The Windows installer has portable tests for first install, backup retention,
rollback, unrelated-folder preservation, missing payloads, running-app refusal,
shortcut path escaping and clean-environment smoke execution. PowerShell syntax
is checked with the real PowerShell parser. The setup banner is tested and
visually inspected at 820px width.

Final local build completed without compiler warnings. All 22 CTest suites
passed in 8.73 seconds, including app smoke, UI, real temporary-repository Git
workflows, runtime checks, packaging verification and nine installer safety
cases. Python compilation, PowerShell parsing and `git diff --check` passed.

Before shipping these changes:

1. Run the updated platform CI, especially the new Windows source installer.
2. Verify actual multi-account OAuth, private clone/fetch/push, publication,
   and existing credential reuse on both installed platforms with authorized
   accounts and disposable repositories.
3. Finish strict bundled-runtime release packaging, platform UX/accessibility
   checks, footprint/startup measurements and distribution approval.

At the end of local validation on 14 September, no signing, notarization,
account login, private remote operation, commit, push, PR merge or release
publication had been performed. On 16 September the user explicitly authorized
committing and pushing these changes to the existing PR for native CI validation.
Release publication and PR merge remain separate steps.
