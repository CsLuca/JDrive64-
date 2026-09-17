# JDrive64 Executable Roadmap

This file tracks actionable tasks with explicit PASS/FAIL criteria.

## Step A1 - WinFsp Real Binding Scaffold

Status: in progress

### A1.1 Runtime abstraction

- Task: introduce a dedicated runtime entry point (`WinFspRuntime`) separated from the CLI and filesystem facade.
- PASS:
  - `include/jdrive64/winfsp_runtime.hpp` exists.
  - `src/winfsp_runtime.cpp` exists.
  - Runtime exposes `StartReadOnly(...)`, `Stop()`, `IsRunning()`, `LastError()`.
- FAIL:
  - No standalone runtime class, or runtime logic is mixed into CLI-only code.

### A1.2 Build gating via CMake option

- Task: wire runtime object into `jdrive64_core` and gate real WinFsp behavior behind `JDRIVE64_ENABLE_WINFSP`.
- PASS:
  - Runtime source is built by default.
  - `JDRIVE64_ENABLE_WINFSP` toggles runtime behavior at compile-time.
- FAIL:
  - Runtime source not linked, or flag has no effect.

### A1.3 Contract test for scaffold behavior

- Task: add core test coverage for runtime in both support-disabled and support-enabled paths.
- PASS:
  - `tests/test_core.cpp` includes runtime checks.
  - Disabled path returns deterministic error text and no running state.
- FAIL:
  - No automated test around runtime scaffold behavior.

### A1.4 Verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`
- Optional WinFsp-on build:
  - `cmake -S . -B build-winfsp -DJDRIVE64_ENABLE_WINFSP=ON`
  - `cmake --build build-winfsp --config Release`

### A1 exit criteria

- Runtime scaffold exists, is test-covered, and passes current suite.
- Next step A2 can focus on wiring real WinFsp callbacks and OS mount lifecycle.
