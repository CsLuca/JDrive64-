# JDrive64 Executable Roadmap

This file tracks actionable tasks with explicit PASS/FAIL criteria.

## Step A1 - WinFsp Real Binding Scaffold

Status: done

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

## Step A2 - Adapter Bridge for Native Callback Wiring

Status: done

### A2.1 Adapter abstraction

- Task: introduce `WinFspAdapter` as bridge layer between runtime lifecycle and filesystem facade.
- PASS:
  - `include/jdrive64/winfsp_adapter.hpp` exists.
  - `src/winfsp_adapter.cpp` exists.
  - Adapter exposes start/stop methods and error surface.
- FAIL:
  - Runtime still couples directly to facade without an adapter bridge.

### A2.2 Runtime delegation

- Task: wire `WinFspRuntime` to delegate start/stop to adapter.
- PASS:
  - Runtime uses adapter methods for lifecycle transitions.
  - Runtime keeps mount-point/running-state logic deterministic.
- FAIL:
  - Runtime bypasses adapter and directly mounts filesystem.

### A2.3 Adapter tests

- Task: add test coverage for adapter disabled and enabled compile-time paths.
- PASS:
  - `tests/test_core.cpp` validates adapter behavior.
  - Disabled path returns deterministic "disabled" message.
- FAIL:
  - No tests for adapter bridge behavior.

### A2 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### A2 exit criteria

- Adapter bridge is in place and tested.
- Runtime no longer depends directly on facade lifecycle for start/stop.
- Next step A3 can introduce native WinFsp callback tables.

## Step A3 - Callback Table Bridge

Status: done

### A3.1 Callback-table abstraction

- Task: introduce a native-style callback table class with function pointers and user-data.
- PASS:
  - `include/jdrive64/winfsp_callbacks.hpp` exists.
  - `src/winfsp_callbacks.cpp` exists.
  - Callback table includes get-volume, readdir, open, read, close dispatch.
- FAIL:
  - No reusable callback table abstraction.

### A3.2 Adapter wiring

- Task: adapter initializes callback table during start and shuts it down during stop.
- PASS:
  - `WinFspAdapter::StartReadOnly` initializes callbacks.
  - `WinFspAdapter::Stop` shuts callbacks down.
  - Adapter exposes callback initialization state.
- FAIL:
  - Adapter lifecycle not connected to callback-table lifecycle.

### A3.3 Callback bridge tests

- Task: test callback dispatch end-to-end against mounted facade.
- PASS:
  - `tests/test_core.cpp` validates dispatch for volume, directory, open/read/close.
  - Dispatch fails after callback shutdown.
- FAIL:
  - No callback bridge regression tests.

### A3 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### A3 exit criteria

- Callback table bridge is integrated and tested.
- Adapter lifecycle fully owns callback-table setup/teardown.
- Next step A4 can wire these tables to real WinFsp native registration.
