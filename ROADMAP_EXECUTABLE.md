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

Status: done

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

## Step A8 - Explorer E2E Validation Readiness

Status: in progress

### A8.1 Preflight command for host validation

- Task: add a CLI preflight command to validate runtime lifecycle on host with WinFsp.
- PASS:
  - `winfsp-preflight <image.d64> <drive_letter:>` command exists.
  - Command attempts runtime start/stop and reports explicit error on failure.
- FAIL:
  - No host-level diagnostic command for runtime lifecycle.

### A8.2 Test and docs wiring

- Task: include preflight command in docs and smoke behavior.
- PASS:
  - `README.md` command list includes `winfsp-preflight`.
  - CLI smoke test checks expected behavior in non-WinFsp environment.
- FAIL:
  - No automated/asserted behavior for preflight command.

### A8.3 Manual Explorer validation checklist

- Task: define manual host checks required to close A8 on a machine with WinFsp installed.
- PASS:
  - Checklist includes mount visibility in Explorer, file browse/read, denied write ops, clean unmount.
- FAIL:
  - Missing actionable E2E closure criteria.

Reference checklist:
- `A8_EXPLORER_CHECKLIST.md`

### A8 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`
- Manual host preflight:
  - `jdrive64 winfsp-preflight <image.d64> <drive_letter:>`

### A8 exit criteria

- Preflight command and smoke/docs are in place.
- Manual Explorer validation executed on a host with WinFsp installed.
- Next step can move to release hardening/package gate.

## Step K0 - Multi-backend Foundation

Status: done

### K0.1 Backend interface

- Task: define a common mount backend interface for runtime selection.
- PASS:
  - `include/jdrive64/mount_backend.hpp` exists.
  - Interface includes `MountReadOnly`, `Unmount`, `HealthCheck` and error access.
- FAIL:
  - Mount path remains hard-wired to a single implementation.

### K0.2 Backend factory and implementations

- Task: implement backend factory with current WinFsp adapter and kernel stub.
- PASS:
  - `src/mount_backend.cpp` exists.
  - `winfsp` backend mounts/unmounts through existing facade.
  - `kdrv` backend returns deterministic "not implemented".
- FAIL:
  - No runtime backend selection, or kdrv path missing.

### K0.3 CLI backend option

- Task: expose backend selection from CLI mount command.
- PASS:
  - `jdrive64 mount <image> <letter> --backend <winfsp|kdrv>` supported.
  - Default mount remains `winfsp` when option is omitted.
- FAIL:
  - No CLI route to select backend.

### K0.4 Tests

- Task: validate backend factory and CLI behavior.
- PASS:
  - Core tests cover backend factory and kdrv not implemented contract.
  - CLI smoke checks `--backend kdrv` expected failure path.
- FAIL:
  - No tests for backend selection behavior.

### K0 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K0 exit criteria

- Multi-backend selection is available and tested.
- WinFsp behavior remains backward-compatible.
- Kernel backend path is stubbed and ready for K1/K2 work.

## K1 - Secure Driver Dev Lab

Status: in progress

### K1.1 VM and safety baseline

- Task: establish isolated VM workflow for kernel driver work.
- PASS:
  - Dedicated VM and snapshot process documented.
  - Policy forbids testing experimental drivers on host workstation.
- FAIL:
  - No isolated lab process.

### K1.2 Tooling baseline

- Task: install and verify WDK + WinDbg toolchain in VM.
- PASS:
  - VS + WDK installed and verified.
  - WinDbg kernel attach and symbol resolution verified.
- FAIL:
  - Debug tooling not operational.

### K1.3 Verifier baseline

- Task: configure Driver Verifier baseline and evidence capture process.
- PASS:
  - Verifier config recorded.
  - Session artifact collection process documented.
- FAIL:
  - No verifier baseline or no artifact process.

### K1 references

- Setup guide: `K1_DRIVER_LAB_SETUP.md`
- Validation record: `K1_LAB_VALIDATION.md`

Current status note:
- Local automated self-check is in place via `scripts/k1-lab-selfcheck.ps1`.
- K1 remains open until VM-only checks in `K1_LAB_VALIDATION.md` are completed.

### K1 exit criteria

- K1 validation record completed with PASS on all mandatory checks.
- Kernel dev lab repeatable and safe for K2 skeleton driver work.

## K2 - Kernel Service Skeleton

Status: done

### K2.1 Kernel controller abstraction

- Task: introduce skeleton controller for kernel service install/start/stop/remove lifecycle.
- PASS:
  - `include/jdrive64/kernel_backend.hpp` exists.
  - `src/kernel_backend.cpp` exists.
  - Lifecycle methods return deterministic errors/states.
- FAIL:
  - No service control abstraction for kdrv path.

### K2.2 kdrv backend scaffold wiring

- Task: connect kdrv mount backend to controller skeleton with deterministic failure contract.
- PASS:
  - `kdrv` mount path executes controller flow and returns explicit not-implemented mount integration error.
  - `kdrv` unmount path handles not-mounted and cleanup states predictably.
- FAIL:
  - kdrv path remains hard-coded placeholder with no lifecycle structure.

### K2.3 Build/test/docs baseline

- Task: wire K2 sources into build and update tests/docs.
- PASS:
  - New kernel backend sources are compiled in core target.
  - Core + CLI smoke tests assert kdrv deterministic failure behavior.
  - README documents kdrv scaffold status.
- FAIL:
  - K2 scaffold not covered in tests/docs.

### K2 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K2 exit criteria

- Service control skeleton exists and is wired in kdrv backend.
- Deterministic not-implemented contract is tested and documented.
- Ready for K3 mount manager + real drive-letter integration.

## K3 - Mount Manager Drive-letter Scaffold

Status: done

### K3.1 Mount manager abstraction

- Task: introduce dedicated mount manager abstraction for drive-letter assignment lifecycle.
- PASS:
  - `include/jdrive64/kernel_mount_manager.hpp` exists.
  - `src/kernel_mount_manager.cpp` exists.
  - API includes assign/release, assigned-state and error accessors.
- FAIL:
  - No dedicated abstraction for drive-letter state.

### K3.2 kdrv integration

- Task: wire mount manager scaffold into kdrv mount/unmount flow.
- PASS:
  - kdrv mount attempts assign-drive-letter stage and surfaces deterministic not-implemented message.
  - kdrv unmount executes release-drive-letter validation path.
- FAIL:
  - kdrv flow does not include mount-manager stage.

### K3.3 Regression coverage

- Task: add tests for mount manager state/error transitions.
- PASS:
  - Core tests cover invalid mount points, deterministic assign failure, state tracking, mismatch release, successful release.
- FAIL:
  - No tests on mount manager scaffold state machine.

### K3 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K3 exit criteria

- Mount manager scaffold is integrated, deterministic, and test-covered.
- kdrv flow is ready for replacement with real mount manager APIs in K4.

## K4 - Read-only Kernel Request Path Scaffold

Status: done

### K4.1 Read-only filesystem contract object

- Task: introduce kernel-side read-only catalog/open/read/close contract object.
- PASS:
  - `include/jdrive64/kernel_readonly_fs.hpp` exists.
  - `src/kernel_readonly_fs.cpp` exists.
  - API includes enumerate/query/open/read/close with deterministic handle semantics.
- FAIL:
  - No dedicated K4 request-path object.

### K4.2 kdrv backend wiring

- Task: wire kdrv backend to use read-only contract object for directory and file read scaffolding.
- PASS:
  - kdrv mount opens image in read-only contract object.
  - kdrv `ReadDirectory()` returns catalog entries via contract object.
  - kdrv status text indicates K4 scaffold progression.
- FAIL:
  - kdrv remains mount-manager-only without read/catalog scaffold.

### K4.3 Test coverage

- Task: add regression tests for read-only contract object.
- PASS:
  - tests cover open image, enumerate, query file, open/read/close handle behavior.
  - closed/invalid handle read path is rejected.
- FAIL:
  - no tests validating K4 read-only contract behavior.

### K4 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K4 exit criteria

- Read-only kernel request path scaffold is integrated and test-covered.
- kdrv backend structure is ready for real IOCTL bridge in K5.

## K5 - IOCTL Bridge Scaffold

Status: done

### K5.1 Message protocol

- Task: define IOCTL-like request/response message schema and opcodes.
- PASS:
  - `include/jdrive64/kernel_ioctl_protocol.hpp` exists.
  - Protocol includes opcodes for read-dir/query/open/read/close.
- FAIL:
  - No formal protocol types for bridge requests.

### K5.2 User-mode bridge dispatcher

- Task: implement bridge dispatcher from protocol messages to K4 read-only handlers.
- PASS:
  - `include/jdrive64/kernel_user_bridge.hpp` exists.
  - `src/kernel_user_bridge.cpp` exists.
  - Dispatcher handles supported opcodes and rejects unsupported opcode.
- FAIL:
  - No dispatch layer between protocol and filesystem contract.

### K5.3 Test coverage

- Task: add regression tests for bridge initialize/dispatch paths.
- PASS:
  - tests cover read-dir/open/read/close opcode flow.
  - invalid opcode returns deterministic error.
- FAIL:
  - bridge protocol path has no test coverage.

### K5 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K5 exit criteria

- IOCTL-like protocol and dispatcher scaffolds are integrated and test-covered.
- Ready for K6 real service IPC wiring to kernel driver channel.

## K6 - Service IPC Channel Scaffold

Status: done

### K6.1 IPC channel abstraction

- Task: introduce service IPC channel object over K5 protocol dispatcher.
- PASS:
  - `include/jdrive64/kernel_ipc_channel.hpp` exists.
  - `src/kernel_ipc_channel.cpp` exists.
  - API includes connect/disconnect/send and connection-state/error accessors.
- FAIL:
  - No dedicated IPC channel abstraction.

### K6.2 Controller lifecycle integration

- Task: wire `KernelBackendController` to own IPC channel lifecycle and request dispatch.
- PASS:
  - controller `StartService` connects IPC.
  - controller `StopService` disconnects IPC.
  - controller exposes read-directory dispatch through IPC.
- FAIL:
  - controller does not use IPC channel for request flow.

### K6.3 kdrv backend path update

- Task: route kdrv directory read through controller IPC dispatch.
- PASS:
  - kdrv `ReadDirectory()` calls controller IPC-backed path.
- FAIL:
  - kdrv still reads directly from K4 object.

### K6.4 Test coverage

- Task: add IPC channel regression tests.
- PASS:
  - tests cover send-before-connect fail, connect/send success, disconnect state.
- FAIL:
  - no test coverage for IPC channel lifecycle.

### K6 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K6 exit criteria

- IPC channel scaffold is integrated in controller and kdrv dispatch path.
- Lifecycle and dispatch behavior are test-covered.
- Ready for K7 real kernel driver transport channel implementation.

## K7 - Transport Mode Scaffold

Status: done

### K7.1 Transport abstraction

- Task: introduce transport abstraction to separate loopback and device-channel modes.
- PASS:
  - `include/jdrive64/kernel_transport.hpp` exists.
  - `src/kernel_transport.cpp` exists.
  - API supports mode selection, connect/disconnect/send.
- FAIL:
  - IPC path has no transport abstraction.

### K7.2 IPC integration

- Task: route IPC channel through transport abstraction.
- PASS:
  - `KernelIpcChannel` delegates connect/send/disconnect to transport.
  - transport mode can be queried/set from IPC channel.
- FAIL:
  - IPC channel still directly owns bridge dispatch.

### K7.3 Device-mode placeholder

- Task: provide placeholder Windows device-channel connect path for future driver wiring.
- PASS:
  - transport attempts opening `\\.\JDrive64Kdrv` in device mode.
  - deterministic not-implemented request path for device mode.
- FAIL:
  - no device-mode path prepared.

### K7.4 Test coverage

- Task: add transport lifecycle tests.
- PASS:
  - tests cover loopback mode send flow.
  - tests cover device mode connect failure contract in current scaffold.
- FAIL:
  - no tests for transport modes.

### K7 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K7 exit criteria

- Transport abstraction is integrated and test-covered.
- Device-mode connect placeholder exists for real driver channel wiring in K8.

## K8 - Device Frame Contract Scaffold

Status: done

### K8.1 Request frame contract

- Task: define deterministic binary request frame encoding for future `DeviceIoControl` path.
- PASS:
  - `KernelTransport::BuildDeviceFrame` encodes opcode, handle, offset, size, and path bytes.
  - encoded frame layout is size-stable and test-covered.
- FAIL:
  - device request payload remains ad-hoc or unspecified.

### K8.2 Response frame contract

- Task: define deterministic binary response frame parsing.
- PASS:
  - `KernelTransport::ParseDeviceFrame` parses success, payload bytes, handle, and error bytes.
  - empty/truncated frames return explicit deterministic response errors.
- FAIL:
  - no parser contract for future driver response payloads.

### K8.3 Device send preflight scaffold

- Task: ensure device mode send path validates encode + parse steps before real IOCTL call wiring.
- PASS:
  - device mode send path invokes request encode and response parse stubs.
  - explicit not-implemented outcome remains deterministic for current scaffold.
- FAIL:
  - send path cannot validate framing stages.

### K8.4 Test coverage

- Task: add tests for request encode and response parse contracts.
- PASS:
  - tests assert encoded request field values and sizes.
  - tests assert empty, truncated, and populated response parsing behavior.
- FAIL:
  - no tests validate frame contracts.

### K8 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K8 exit criteria

- Device frame contracts are codified and regression-tested.
- Transport layer is ready for K9 real `DeviceIoControl` wiring.

## K9 - DeviceIo Pipeline Scaffold

Status: done

### K9.1 Injectable device IO abstraction

- Task: add explicit device-IO abstraction for open/ioctl/close to make transport testable.
- PASS:
  - `KernelTransport::DeviceIoApi` exposes `Open`, `Ioctl`, `Close`.
  - transport supports test injection via `SetDeviceIoApiForTesting`.
- FAIL:
  - device path remains hard-wired and untestable.

### K9.2 Device-mode send pipeline

- Task: wire device mode send through request encode -> ioctl -> response parse.
- PASS:
  - device mode send builds request frame, invokes IOCTL API, parses response frame.
  - deterministic error propagation for encode/ioctl/parse failures.
- FAIL:
  - send path does not exercise IOCTL stage.

### K9.3 Windows default implementation

- Task: provide default Windows implementation using `CreateFileA`/`DeviceIoControl`/`CloseHandle`.
- PASS:
  - default API opens `\\.\JDrive64Kdrv` and issues a dedicated control code placeholder.
  - non-Windows path returns explicit unsupported message.
- FAIL:
  - no default Windows device I/O implementation.

### K9.4 Regression tests

- Task: add tests for injected API success/failure behavior.
- PASS:
  - tests validate open/ioctl/close call flow and request frame forwarding.
  - tests validate surfaced IOCTL failure text.
- FAIL:
  - no tests around device-mode IO pipeline.

### K9 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K9 exit criteria

- Device-mode transport pipeline is integrated and test-covered.
- Ready for K10 driver-side protocol/version handshake alignment.

## K10 - Handshake and Capability Negotiation Scaffold

Status: done

### K10.1 Protocol constants and opcode

- Task: formalize transport protocol version and read-only capability bitmask with dedicated handshake opcode.
- PASS:
  - protocol constants exist in `kernel_ioctl_protocol.hpp`.
  - `KernelOpcode::kHandshake` exists and is used by device connect flow.
- FAIL:
  - no protocol version/capability constants.

### K10.2 Connect-time handshake

- Task: execute handshake immediately after device open.
- PASS:
  - device connect sends handshake request including protocol version and requested capabilities.
  - connect fails and closes handle on handshake failure.
- FAIL:
  - device connect does not perform handshake.

### K10.3 Negotiation enforcement

- Task: require completed handshake before non-handshake requests and expose negotiated state.
- PASS:
  - non-handshake sends fail when handshake is incomplete.
  - transport exposes handshake completion and negotiated capabilities.
- FAIL:
  - non-handshake requests can run before negotiation.

### K10.4 Regression tests

- Task: add tests for handshake success and mismatch failures.
- PASS:
  - tests validate handshake success path and negotiated capabilities.
  - tests validate protocol mismatch and insufficient capability failures.
- FAIL:
  - no tests for negotiation behavior.

### K10 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K10 exit criteria

- Handshake/version/capability negotiation is integrated and test-covered.
- Ready for K11 driver contract expansion beyond read-only baseline.

## K11 - Compatibility Matrix and Feature Policy Scaffold

Status: in progress

### K11.1 Protocol compatibility window

- Task: accept handshake protocol versions within a bounded compatibility window.
- PASS:
  - protocol constants include `min/current/max` values.
  - handshake accepts compatible range and rejects out-of-range versions.
- FAIL:
  - transport supports only exact single protocol version.

### K11.2 Feature-flag negotiation

- Task: include feature flags in handshake contract with required-feature policy.
- PASS:
  - handshake request includes requested feature flags.
  - handshake response parses negotiated feature flags.
  - connect fails when required feature bits are missing.
- FAIL:
  - no feature policy in handshake negotiation.

### K11.3 Negotiated-state accessors

- Task: expose negotiated protocol version and features for upper-layer policy decisions.
- PASS:
  - transport exposes negotiated protocol version and features in addition to capabilities.
  - values reset on disconnect/failure paths.
- FAIL:
  - negotiated state is not queryable.

### K11.4 Regression tests

- Task: add tests for compatibility-range and feature-mismatch behavior.
- PASS:
  - tests validate compatible minimum version success.
  - tests validate feature mismatch failure.
- FAIL:
  - no tests for compatibility matrix behavior.

### K11 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K11 exit criteria

- Compatibility matrix and feature policy are integrated and test-covered.
- Ready for K12 command-level policy routing based on negotiated contract.

Release gate reference:
- `V1_RELEASE_CHECKLIST.md`
