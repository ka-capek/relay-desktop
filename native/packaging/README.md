# Native release packaging

Packaging is deliberately opt-in. Ordinary development builds do not require
the ignored `runtime/` tree. Release configuration fails closed when a bundled
runtime, license, corresponding-source URI, deployment plugin, or clean-stage
verification is missing.

## Required release inputs

- An official, dynamically linked Qt build matching `RELAY_QT_MIN_VERSION`.
  Homebrew Qt is suitable for development but not the release baseline: its
  frameworks link to local Homebrew ICU, GLib, OpenSSL, HarfBuzz, and other
  kegs. Staged verification rejects `/opt/homebrew`, `/usr/local`, source-tree,
  and build-tree dependencies.
- `runtime/git/mac-arm64` and `runtime/gh/mac-arm64` on macOS, or
  `runtime/git/win-x64` and `runtime/gh/win-x64` on Windows. `runtime/` is
  intentionally ignored by Git and must be provisioned outside source control.
- Relay's tracked root `LICENSE` plus exact Qt LGPL, Git GPL, and GitHub CLI MIT
  texts. Set `RELAY_QT_LICENSE_FILE`, `RELAY_GIT_LICENSE_FILE`, or
  `RELAY_GH_LICENSE_FILE` when they are not present in the runtime/SDK.
- `RELAY_GIT_SOURCE_URI` pointing to a durable corresponding-source artifact
  for the exact binary payload. For Git for Windows it must cover the shipped
  MSYS2 component inventory, not merely a `git.git` archive. Qt and `gh` source
  URIs default from their pinned versions but should be mirrored with release
  assets so availability does not depend on a third party.

The current ignored payload is expected to report Git 2.53.0 and gh 2.98.0 on
macOS, and Git 2.52.0.windows.1 and gh 2.98.0 on Windows. Override the
`RELAY_BUNDLED_*_VERSION` cache variables only when deliberately replacing the
payload and its corresponding-source record.

## CMake integration

Replace the standalone `qt_generate_deploy_app_script()` block in
`native/CMakeLists.txt` with:

```cmake
include(cmake/RelayPackaging.cmake)
relay_configure_packaging(TARGET relay_native)
```

Keep the existing `install(TARGETS relay_native ...)` before those lines.
Runtime payloads and notices are installed first, Qt deployment runs after
them, and stage verification runs last so the final macOS bundle seal includes
every nested executable.

Configure with an official Qt prefix and a release-stable Git source bundle:

```sh
cmake --preset macos-release \
  -DRELAY_ENABLE_PACKAGING=ON \
  -DRELAY_PACKAGE_STRICT=ON \
  -DRELAY_GIT_SOURCE_URI=https://example.invalid/relay-sources/git-mac-arm64-2.53.0.tar.xz
cmake --build --preset macos-release
cpack --config build-native/macos-release/CPackConfig.cmake
```

Run the analogous commands from an x64 MSVC developer environment on Windows.
NSIS (`makensis`) must be available there. Release packaging is native-only;
cross-packaging is intentionally rejected.

Artifacts are written to `outputs/native-installers/` and receive a CPack
SHA-256 sidecar. macOS uses CPack DragNDrop (`.dmg`); Windows uses interactive
NSIS with desktop and Start Menu shortcuts. Empty macOS signing identity means
ad-hoc signing. Proper distribution still requires Developer ID signing and
notarization on macOS and Authenticode signing on Windows.

## Verification and measurement

The install step runs `native/tools/verify_staged_release.cmake`. It checks the
application, Qt platform/image/TLS plugins, Git HTTPS support, templates,
Windows CA bundle, `gh`, notices, architecture, absence of excluded Git
Credential Manager/LFS/debug payloads, macOS linkage, and bundle signature.

Record each Phase-1 scenario separately after putting the application into the
specified state:

```sh
python3 native/tools/measure_release.py \
  --scenario idle \
  --stage build-native/stage/Relay.app \
  --artifact outputs/native-installers/Relay-0.6.0-arm64.dmg \
  --pid 12345 \
  --output outputs/native-measurements/macos-idle.json
```

Repeat for `repository-open`, `large-diff`, and `long-history`, on macOS and
Windows, using the same fixtures for Electron and native Relay. macOS records
`footprint` physical footprint and dirty memory. Windows records private bytes
and working set through PowerShell. The JSON also records apparent/allocated
installed size, artifact bytes and SHA-256, process details, machine/OS, UTC
time, and the exact invocation. Do not compare summed RSS with physical
footprint/private bytes.
