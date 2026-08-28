# Relay Native measurements

This file records reproducible measurements for the C++/Qt rewrite. Results are
kept separate from planning ranges in `PLAN.md`.

## 2026-08-26 — first macOS development baseline

Environment:

- macOS 15.7.3, Apple Silicon
- Apple Clang 17.0.0
- CMake 4.4.3, Ninja 1.13.2
- Qt 6.11.1 from Homebrew, dynamically linked
- Debug build, empty state, no repository open
- Offscreen Qt platform plugin, so this is an engineering baseline rather than
  the final packaged-GUI number

Build and launch:

```bash
cmake --preset macos-debug
cmake --build --preset macos-debug --parallel
QT_QPA_PLATFORM=offscreen \
  build-native/macos-debug/native/Relay.app/Contents/MacOS/Relay
```

Measurement commands, where `<pid>` is the Relay process:

```bash
ps -o pid=,rss=,vsz=,etime=,command= -p <pid>
footprint -p <pid>
```

Observed:

| Metric | Result |
| --- | ---: |
| RSS reported by `ps` | 46,432 KB |
| Physical footprint | 18 MB |
| Peak physical footprint | 18 MB |
| Processes | 1 |

This is below the provisional 80 MB idle target, but it is not the Phase 1
gate. The gate still requires an installer made with the pinned official Qt
distribution, a normal on-screen launch, the full vertical slice, and the same
idle/repository/diff/history scenarios on macOS arm64 and Windows x64.
