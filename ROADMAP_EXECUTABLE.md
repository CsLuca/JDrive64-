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

## Step A4 - Native Registration Bridge

Status: done

### A4.1 Native bridge abstraction

- Task: introduce a native registration bridge object that validates callback table readiness.
- PASS:
  - `include/jdrive64/winfsp_native_bridge.hpp` exists.
  - `src/winfsp_native_bridge.cpp` exists.
  - Bridge exposes register/unregister and state/error accessors.
- FAIL:
  - No dedicated bridge for native registration lifecycle.

### A4.2 Adapter lifecycle integration

- Task: wire adapter start/stop to native registration bridge with rollback behavior.
- PASS:
  - Start initializes callbacks then registers bridge.
  - Start rollback path unmounts and shuts callbacks on registration failure.
  - Stop unregisters bridge before callback shutdown/unmount.
- FAIL:
  - Adapter can leave partially initialized state on failure.

### A4.3 Native bridge tests

- Task: add regression tests for native bridge and adapter registration state.
- PASS:
  - `tests/test_core.cpp` validates native bridge disabled/enabled contract.
  - Adapter tests assert callback and native registration states.
- FAIL:
  - No tests covering bridge state transitions.

### A4 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### A4 exit criteria

- Native registration bridge abstraction is integrated and tested.
- Adapter lifecycle is transactional across callback/native/mount stages.
- Next step A5 can replace bridge internals with real WinFsp registration APIs.

## Step A5 - Native API Provider Injection

Status: done

### A5.1 Native API provider interface

- Task: introduce an injectable native API provider used by the bridge.
- PASS:
  - `include/jdrive64/winfsp_native_api.hpp` exists.
  - `src/winfsp_native_api.cpp` exists.
  - Provider interface supports register/unregister with explicit error propagation.
- FAIL:
  - Bridge has hard-coded registration logic with no provider abstraction.

### A5.2 Bridge delegation

- Task: refactor native bridge to delegate register/unregister to provider.
- PASS:
  - Bridge owns default provider but supports `SetApi(...)` injection.
  - Bridge preserves current state contract and mount-point tracking.
- FAIL:
  - Bridge cannot be tested with fake provider or cannot swap provider.

### A5.3 Provider-injection tests

- Task: add tests with fake provider for success/failure transitions.
- PASS:
  - `tests/test_core.cpp` validates register failure, register success,
    unregister failure, unregister success with fake provider.
  - Error text from provider is surfaced by bridge.
- FAIL:
  - No deterministic tests for provider failure paths.

### A5 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### A5 exit criteria

- Bridge delegates all registration actions to provider.
- Provider injection tests pass for success/failure paths.
- Next step A6 can implement real WinFsp FSP API calls in default provider.

## Step A6 - Default Provider Bootstrap Readiness

Status: done

### A6.1 WinFsp DLL bootstrap

- Task: make default provider load WinFsp runtime DLL dynamically.
- PASS:
  - Default provider attempts loading `winfsp-x64.dll` then `winfsp.dll`.
  - Error is explicit when DLL is missing.
- FAIL:
  - No dynamic loading path in default provider.

### A6.2 Symbol resolution preflight

- Task: validate required WinFsp entry points before claiming registration.
- PASS:
  - Provider resolves `FspFileSystemCreate`, `FspFileSystemSetMountPoint`,
    `FspFileSystemStartDispatcher`, `FspFileSystemStopDispatcher`, `FspFileSystemDelete`.
  - Missing symbol errors are explicit.
- FAIL:
  - Provider reports success without symbol checks.

### A6.3 Registration state contract

- Task: enforce deterministic register/unregister state transitions in default provider.
- PASS:
  - Duplicate register attempts fail with clear error.
  - Unregister without active registration fails with clear error.
- FAIL:
  - Provider state transitions are ambiguous or silent.

### A6 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### A6 exit criteria

- Default provider has bootstrap/symbol preflight for real WinFsp wiring.
- Registration state contract is deterministic.
- Next step A7 can replace preflight-only path with real `FspFileSystem*` lifecycle calls.

## Step A7 - Default Provider Real FSP Lifecycle

Status: in progress

### A7.1 Create/mount/start dispatcher flow

- Task: execute real `FspFileSystem*` lifecycle in default provider after bootstrap.
- PASS:
  - Provider calls create -> set mount point -> start dispatcher.
  - Failures at any stage are handled with cleanup.
- FAIL:
  - Provider only preflights symbols without lifecycle calls.

### A7.2 Stop/delete cleanup flow

- Task: implement stop dispatcher and delete filesystem on unregister/destruction.
- PASS:
  - Unregister stops dispatcher then deletes filesystem.
  - Destructor cleans up if unregister was not called.
- FAIL:
  - Provider leaks active dispatcher or filesystem handle.

### A7.3 Provider contract tests

- Task: validate provider contract remains deterministic under disabled and partial-runtime scenarios.
- PASS:
  - `tests/test_core.cpp` covers default provider behavior and explicit error outputs.
- FAIL:
  - Provider behavior regresses without tests.

### A7 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### A7 exit criteria

- Default provider executes real create/mount/start and stop/delete lifecycle calls.
- Cleanup is robust on failure and shutdown.
- Next step A8 can focus on end-to-end Explorer mount validation.
