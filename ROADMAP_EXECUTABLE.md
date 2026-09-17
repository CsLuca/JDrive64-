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

Status: done

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

## K12 - Command-level Policy Routing Scaffold

Status: done

### K12.1 Opcode policy evaluator

- Task: introduce explicit per-opcode policy evaluation against negotiated contract.
- PASS:
  - transport exposes `IsRequestAllowedByPolicy(...)`.
  - evaluator enforces capability mapping for read-directory/query/read/open/close paths.
- FAIL:
  - no standalone policy evaluator.

### K12.2 Send-path policy enforcement

- Task: gate non-handshake device requests through policy evaluator before IOCTL dispatch.
- PASS:
  - `Send(...)` rejects policy-denied requests before frame encode/ioctl.
  - rejection error text is explicit and deterministic.
- FAIL:
  - send path bypasses policy checks.

### K12.3 Policy diagnostics

- Task: provide actionable denial reasons for unsupported opcode/capability mismatch.
- PASS:
  - denial reason includes operation and root cause.
  - invalid opcode receives dedicated unsupported message.
- FAIL:
  - denial reason is generic or absent.

### K12.4 Regression tests

- Task: add tests for policy evaluator allow/deny behavior.
- PASS:
  - tests validate allow decisions for supported operations.
  - tests validate deny decisions and explicit messages for unsupported opcode.
- FAIL:
  - no regression tests for policy routing.

### K12 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K12 exit criteria

- Command-level policy routing is integrated and test-covered.
- Ready for K13 richer operation policy and driver feature rollout strategy.

## K13 - Feature Rollout Strategy Scaffold

Status: done

### K13.1 Feature policy modes

- Task: introduce strict/best-effort feature policy modes in transport.
- PASS:
  - transport exposes feature policy setters/getters.
  - strict mode requires full default feature set during handshake.
  - best-effort mode accepts reduced optional features while enforcing required ones.
- FAIL:
  - no selectable feature policy mode.

### K13.2 Granular handle-operation gating

- Task: enforce dedicated feature requirement for handle-oriented ops.
- PASS:
  - `OpenFile`/`CloseFile` require stable-handle feature.
  - denial reason is explicit when stable-handle feature is missing.
- FAIL:
  - handle operations are not feature-gated.

### K13.3 Runtime policy immutability while connected

- Task: prevent policy changes mid-session to avoid inconsistent contracts.
- PASS:
  - changing feature policy while connected fails deterministically.
  - error text indicates connected-state lock.
- FAIL:
  - policy can be changed while transport is connected.

### K13.4 Regression tests

- Task: add tests for strict vs best-effort behavior and handle-op gating.
- PASS:
  - tests validate strict mode rejection of missing optional feature.
  - tests validate best-effort connect with reduced features plus handle-op denial.
  - tests validate policy change rejection while connected.
- FAIL:
  - no coverage for rollout modes.

### K13 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K13 exit criteria

- Rollout policy mode and granular feature gating are integrated and test-covered.
- Ready for K14 backend-level policy surfacing and CLI diagnostics.

## K14 - Backend Diagnostics Surfacing Scaffold

Status: done

### K14.1 Backend diagnostics interface

- Task: expose backend-level diagnostics text across mount backend implementations.
- PASS:
  - `IMountBackend` includes diagnostics accessor.
  - winfsp and kdrv backends provide deterministic diagnostics payloads.
- FAIL:
  - no backend diagnostics contract.

### K14.2 Kernel diagnostics payload

- Task: surface negotiated transport state from kernel backend controller.
- PASS:
  - diagnostics include transport mode, feature policy, handshake state, and negotiated values.
  - diagnostics are available without ad-hoc CLI internals.
- FAIL:
  - negotiated kernel state is not surfaced.

### K14.3 CLI command integration

- Task: add CLI command to display backend diagnostics against an image and backend selection.
- PASS:
  - `backend-diag <image.d64> [--backend <winfsp|kdrv>]` works.
  - command prints requested backend and diagnostics text.
- FAIL:
  - no CLI surface for backend diagnostics.

### K14.4 Regression coverage

- Task: add core/smoke tests for diagnostics surface.
- PASS:
  - core tests validate kernel diagnostics text includes key fields.
  - CLI smoke validates `backend-diag` for kdrv.
- FAIL:
  - diagnostics path has no test coverage.

### K14 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K14 exit criteria

- Backend diagnostics are surfaced consistently in core and CLI layers.
- Ready for K15 persistence/telemetry of negotiated backend state.

## K15 - Persisted Diagnostics Snapshot Scaffold

Status: done

### K15.1 Mount-state schema extension

- Task: persist backend identity and diagnostics snapshot in mount-state files.
- PASS:
  - mount state writes `BACKEND=` and `DIAG=` records.
  - mount state parser reads persisted backend and diagnostics lines.
- FAIL:
  - negotiated diagnostics are not persisted.

### K15.2 Mounted diagnostics command

- Task: expose persisted diagnostics through mounted-state CLI command.
- PASS:
  - `backend-diag-mounted <drive_letter:>` is available.
  - command reports mount, image, backend, and persisted diagnostics.
- FAIL:
  - no command to inspect persisted diagnostics.

### K15.3 Backward-compatible error handling

- Task: keep deterministic validation for malformed or incomplete mount-state payloads.
- PASS:
  - parser returns explicit errors for invalid state format.
  - command returns actionable errors for missing/unmounted state.
- FAIL:
  - parser silently accepts malformed state.

### K15.4 Regression coverage

- Task: add smoke coverage for mounted diagnostics persistence path.
- PASS:
  - CLI smoke validates `backend-diag-mounted` after mount.
  - output includes persisted backend marker.
- FAIL:
  - no test coverage for mounted diagnostics path.

### K15 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K15 exit criteria

- Persisted diagnostics snapshot path is integrated and test-covered.
- Ready for K16 telemetry/export integration.

## K16 - JSON Diagnostics Export Scaffold

Status: done

### K16.1 JSON encoder utility

- Task: add safe JSON string escaping for diagnostics output.
- PASS:
  - CLI includes deterministic JSON escaping helper.
  - output remains valid for quotes/backslashes/control newlines.
- FAIL:
  - JSON output can break on unescaped content.

### K16.2 Live diagnostics JSON output

- Task: support `--json` for live backend diagnostics command.
- PASS:
  - `backend-diag ... --json` emits machine-readable object.
  - output includes image, requested backend, and diagnostics array.
- FAIL:
  - live diagnostics command is text-only.

### K16.3 Mounted diagnostics JSON output

- Task: support `--json` for persisted mounted diagnostics command.
- PASS:
  - `backend-diag-mounted ... --json` emits machine-readable object.
  - output includes mount, image, backend, and diagnostics array.
- FAIL:
  - mounted diagnostics command is text-only.

### K16.4 Regression coverage

- Task: add smoke tests for both JSON diagnostics modes.
- PASS:
  - smoke tests validate `backend-diag --json` and `backend-diag-mounted --json` exit 0.
  - smoke tests assert key JSON fields.
- FAIL:
  - no tests for JSON diagnostics exports.

### K16 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K16 exit criteria

- JSON diagnostics export is integrated and test-covered.
- Ready for K17 telemetry sink integration and structured event emission.

## K17 - Structured Transport Telemetry Scaffold

Status: done

### K17.1 Telemetry sink abstraction

- Task: add transport-level telemetry sink abstraction with structured event payload.
- PASS:
  - transport exposes `TelemetryEvent` and `TelemetrySink` interfaces.
  - tests can inject sink through transport API.
- FAIL:
  - no sink abstraction for structured telemetry.

### K17.2 Event emission points

- Task: emit success/failure events across critical transport lifecycle stages.
- PASS:
  - events emitted for connect/send/disconnect paths and key failure branches.
  - event payload carries stage name, success flag, and detail text.
- FAIL:
  - transport lifecycle has no structured event emission.

### K17.3 Regression tests

- Task: validate telemetry events in loopback/device success and failure scenarios.
- PASS:
  - tests assert loopback connect/send/disconnect telemetry success events.
  - tests assert device IOCTL failure telemetry event.
- FAIL:
  - no tests for telemetry behavior.

### K17 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K17 exit criteria

- Structured telemetry sink and event emission are integrated and test-covered.
- Ready for K18 persistent telemetry sink and export pipeline.

## K18 - Persistent Telemetry JSONL Sink Scaffold

Status: done

### K18.1 JSONL telemetry sink implementation

- Task: add persistent telemetry sink writing one JSON event per line.
- PASS:
  - dedicated JSONL sink class exists.
  - sink safely escapes JSON string fields and appends events.
- FAIL:
  - no persistent telemetry sink implementation.

### K18.2 Backend wiring

- Task: allow kernel backend to enable JSONL telemetry sink via environment configuration.
- PASS:
  - kernel backend enables sink when `JDRIVE64_TELEMETRY_JSONL` is set.
  - diagnostics report telemetry sink status.
- FAIL:
  - telemetry sink cannot be enabled from runtime config.

### K18.3 Regression tests

- Task: validate JSONL sink persistence path.
- PASS:
  - tests emit transport telemetry through JSONL sink.
  - tests validate output file contains expected event names.
- FAIL:
  - no tests for persistent telemetry sink.

### K18 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K18 exit criteria

- Persistent JSONL telemetry sink is integrated and test-covered.
- Ready for K19 telemetry rotation/export management.

## K19 - Telemetry Rotation and Mounted Management Scaffold

Status: done

### K19.1 Rotation/size cap

- Task: add telemetry JSONL file size cap with single-step rotation.
- PASS:
  - sink supports configurable max bytes.
  - file rotates to `.1` when size exceeds cap.
- FAIL:
  - telemetry file can grow unbounded.

### K19.2 Mounted telemetry path persistence

- Task: persist telemetry JSONL path in mount state for mounted operations.
- PASS:
  - mount state stores `TELEMETRY_JSONL=` value.
  - mounted diagnostics expose persisted telemetry path.
- FAIL:
  - mounted state does not track telemetry path.

### K19.3 Mounted telemetry management commands

- Task: add commands to inspect/clear telemetry from mounted context.
- PASS:
  - `telemetry-dump-mounted <drive_letter:>` outputs telemetry JSONL contents.
  - `telemetry-clear-mounted <drive_letter:>` truncates telemetry file.
- FAIL:
  - no mounted telemetry management commands.

### K19.4 Regression coverage

- Task: test rotation and mounted telemetry command paths.
- PASS:
  - core tests validate rotation behavior.
  - CLI smoke validates telemetry dump/clear commands.
- FAIL:
  - no coverage for telemetry rotation/management.

### K19 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K19 exit criteria

- Telemetry rotation and mounted management are integrated and test-covered.
- Ready for K20 telemetry retention policy and multi-file indexing.

## K20 - Telemetry Retention and Indexing Scaffold

Status: done

### K20.1 Multi-file retention

- Task: support rotating telemetry files across multiple generations.
- PASS:
  - sink accepts max retained file count.
  - rotation chain produces `.1`, `.2`, ... up to configured limit.
- FAIL:
  - retention supports only a single rotated file.

### K20.2 Mounted telemetry index command

- Task: add mounted command that lists available telemetry files with size metadata.
- PASS:
  - `telemetry-list-mounted <drive_letter:>` outputs base/rotated files.
  - output includes size info and no-crash behavior on missing files.
- FAIL:
  - no index/list command for telemetry files.

### K20.3 Regression coverage

- Task: validate retention chain and list command behavior.
- PASS:
  - core tests validate second-generation rotation file existence.
  - CLI smoke validates telemetry list command output.
- FAIL:
  - no regression tests for retention index behavior.

### K20 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K20 exit criteria

- Multi-file retention and mounted indexing are integrated and test-covered.
- Ready for K21 event filters and query options.

## K21 - Telemetry Query Filter Scaffold

Status: done

### K21.1 Filter parsing

- Task: add query options for telemetry dump command.
- PASS:
  - `telemetry-dump-mounted` supports `--event`, `--success`, `--tail`.
  - invalid/missing option values fail with usage.
- FAIL:
  - telemetry dump has no query options.

### K21.2 Filter evaluation

- Task: filter JSONL telemetry lines by event name and success flag with tail selection.
- PASS:
  - event-name and success filters work against serialized JSON lines.
  - `--tail N` limits output to last N matched lines.
- FAIL:
  - filters do not affect output.

### K21.3 Regression coverage

- Task: validate telemetry dump filter mode in CLI smoke.
- PASS:
  - smoke test runs filtered telemetry dump command successfully.
  - no regressions for unfiltered dump/clear/list commands.
- FAIL:
  - no tests for query filter mode.

### K21 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K21 exit criteria

- Telemetry query filters are integrated and test-covered.
- Ready for K22 structured query output modes and pagination.

## K22 - Telemetry Query JSON and Pagination Scaffold

Status: done

### K22.1 Query output mode

- Task: add structured JSON output mode for filtered telemetry dumps.
- PASS:
  - `telemetry-dump-mounted ... --json` emits object with metadata and entries.
  - entries are JSON-escaped serialized telemetry lines.
- FAIL:
  - filtered telemetry supports text output only.

### K22.2 Pagination options

- Task: add offset/limit pagination over filtered telemetry results.
- PASS:
  - `--offset` and `--limit` are parsed and applied.
  - pagination works with filters and `--tail` preselection.
- FAIL:
  - no pagination controls for filtered telemetry dumps.

### K22.3 Regression coverage

- Task: validate JSON+paging telemetry query in CLI smoke.
- PASS:
  - smoke runs filtered/paginated JSON dump command successfully.
  - smoke validates JSON key presence (`entries`).
- FAIL:
  - no tests for JSON query mode.

### K22 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K22 exit criteria

- Telemetry query JSON output and pagination are integrated and test-covered.
- Ready for K23 richer query DSL and cross-file search.

## K23 - Cross-file Telemetry Query Scaffold

Status: done

### K23.1 Cross-file telemetry merge

- Task: query telemetry over base and rotated retention files.
- PASS:
  - dump/query commands scan base + rotated chain.
  - merged output preserves deterministic old->new ordering.
- FAIL:
  - queries read only base telemetry file.

### K23.2 Retention tuning from environment

- Task: allow runtime tuning of telemetry retention parameters.
- PASS:
  - sink max bytes/files can be set via environment variables.
  - invalid values fall back safely to defaults.
- FAIL:
  - retention parameters are hardcoded only.

### K23.3 Regression coverage

- Task: validate rotated file visibility in CLI smoke.
- PASS:
  - smoke asserts rotated telemetry file presence via telemetry list command.
  - query path remains green with filters/pagination/json.
- FAIL:
  - no tests that exercise rotated file querying.

### K23 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K23 exit criteria

- Cross-file telemetry query and retention tuning are integrated and test-covered.
- Ready for K24 richer query DSL and aggregation stats.

## K24 - Telemetry Aggregation Stats Scaffold

Status: done

### K24.1 Aggregation command

- Task: provide mounted telemetry aggregation command with per-event metrics.
- PASS:
  - `telemetry-stats-mounted <drive_letter:>` exists.
  - command reports global totals and per-event counts/success counts.
- FAIL:
  - no mounted telemetry aggregation command.

### K24.2 JSON aggregation output

- Task: support machine-readable stats output.
- PASS:
  - `telemetry-stats-mounted ... --json` emits totals and events array.
  - each event includes `count`, `success_count`, `success_rate`.
- FAIL:
  - aggregation command has text output only.

### K24.3 Cross-file aggregation scope

- Task: aggregate over full retention chain (base + rotated files).
- PASS:
  - stats command scans collected telemetry files from retention chain.
  - missing files are tolerated without crashes.
- FAIL:
  - aggregation only uses base telemetry file.

### K24.4 Regression coverage

- Task: validate stats command text/json in CLI smoke.
- PASS:
  - smoke validates text totals and JSON events field.
  - existing dump/list/clear/query coverage remains green.
- FAIL:
  - no tests for telemetry stats command.

### K24 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K24 exit criteria

- Telemetry aggregation stats are integrated and test-covered.
- Ready for K25 query DSL refinements and export bundling.

## K25 - Telemetry Query DSL and Export Bundle Scaffold

Status: done

### K25.1 Query DSL refinements

- Task: extend telemetry query filtering to support include/exclude/pattern matching.
- PASS:
  - `telemetry-dump-mounted` supports repeated `--event <name>` includes.
  - command supports `--exclude-event <name>`, `--event-prefix <prefix>`, and `--event-contains <text>`.
- FAIL:
  - query filter supports exact single event only.

### K25.2 Export bundle mode

- Task: add bundle output that combines query entries with stats metadata in a single JSON result.
- PASS:
  - `telemetry-dump-mounted ... --bundle` emits JSON with entries + stats.
  - bundle includes scope metadata (`mount`, `files_scanned`, `total_matched`).
- FAIL:
  - no single-command bundled export is available.

### K25.3 Regression coverage

- Task: validate refined DSL and bundle export in CLI smoke tests.
- PASS:
  - smoke runs commands with new DSL options successfully.
  - smoke asserts bundle JSON contains stats metadata fields.
- FAIL:
  - no test coverage for DSL/bundle behavior.

### K25 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K25 exit criteria

- Query DSL refinements and bundled export are integrated and test-covered.
- Ready for K26 advanced selectors and export schema stabilization.

## K26 - Telemetry Selector Semantics and Export Schema Scaffold

Status: done

### K26.1 Selector semantics

- Task: support explicit selector combination mode for positive event selectors.
- PASS:
  - `telemetry-dump-mounted` accepts `--selector-mode <all|any>`.
  - `all` enforces conjunction across positive selectors; `any` accepts disjunction.
- FAIL:
  - selector combination behavior is fixed/implicit only.

### K26.2 Export schema metadata

- Task: stabilize JSON export shape with schema and query metadata.
- PASS:
  - JSON output includes stable `schema_version` marker.
  - JSON output includes `query` object with selector/filter parameters.
- FAIL:
  - JSON output has no explicit schema marker or query metadata.

### K26.3 Regression coverage

- Task: validate selector-mode and schema metadata in CLI smoke tests.
- PASS:
  - smoke runs `--selector-mode any` query successfully.
  - smoke asserts `schema_version` and `query.selector_mode` fields.
- FAIL:
  - no tests for selector semantics and schema metadata.

### K26 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K26 exit criteria

- Selector semantics and schema metadata are integrated and test-covered.
- Ready for K27 query expression normalization and schema versioning policy.

## K27 - Query Normalization and Schema Policy Scaffold

Status: done

### K27.1 Query normalization

- Task: normalize telemetry query options into deterministic internal/output representation.
- PASS:
  - include/exclude event lists are trimmed, deduplicated, and stable-ordered.
  - selector mode is normalized for selector-less positive query cases.
- FAIL:
  - equivalent queries can produce unstable metadata/output representation.

### K27.2 Schema versioning policy metadata

- Task: include explicit schema evolution policy metadata in telemetry JSON exports.
- PASS:
  - telemetry query JSON includes `schema_policy` object.
  - policy states compatibility model for major/minor evolution.
- FAIL:
  - no explicit schema policy metadata in export payload.

### K27.3 Regression coverage

- Task: validate normalization and schema policy fields in CLI smoke tests.
- PASS:
  - smoke asserts `schema_policy.versioning` field presence.
  - smoke validates normalized selector mode when positive selectors are absent.
- FAIL:
  - no tests for normalization/policy metadata behavior.

### K27 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K27 exit criteria

- Query normalization and schema policy metadata are integrated and test-covered.
- Ready for K28 structured expression parser scaffold.

## K28 - Structured Query Expression Parser Scaffold

Status: done

### K28.1 Where expression syntax

- Task: add compact expression syntax for telemetry query filtering.
- PASS:
  - `telemetry-dump-mounted` supports `--where <expr>`.
  - expression supports `AND`/`OR` over supported predicates (`event==...`, `event_prefix==...`, `event_contains==...`, `success==true|false`).
- FAIL:
  - no parser for structured telemetry query expressions.

### K28.2 Normalized expression export

- Task: expose canonicalized expression string in query JSON metadata.
- PASS:
  - JSON query object includes normalized `where` field.
  - invalid expressions fail with explicit parse error.
- FAIL:
  - expression metadata missing or parser errors are non-actionable.

### K28.3 Regression coverage

- Task: validate where-expression mode in CLI smoke.
- PASS:
  - smoke executes `--where` query successfully.
  - smoke asserts normalized `query.where` field.
- FAIL:
  - no tests for expression parser path.

### K28 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K28 exit criteria

- Structured expression parser scaffold is integrated and test-covered.
- Ready for K29 expression precedence/grouping scaffold.

## K29 - Expression Precedence and Grouping Scaffold

Status: done

### K29.1 Precedence-aware parser

- Task: add deterministic operator precedence for telemetry where-expression evaluation.
- PASS:
  - parser enforces `AND` precedence above `OR`.
  - malformed operator chains still fail with explicit errors.
- FAIL:
  - expression evaluation remains strict left-to-right only.

### K29.2 Parenthesis grouping

- Task: support grouped conditions with `(` and `)`.
- PASS:
  - parser accepts grouped expressions and rejects unmatched parentheses.
  - grouped expressions evaluate via compiled expression form.
- FAIL:
  - no parenthesis support in expression parser.

### K29.3 Regression coverage

- Task: validate grouped expression queries in CLI smoke.
- PASS:
  - smoke executes grouped `--where` expression successfully.
  - smoke asserts normalized grouped expression in JSON query metadata.
- FAIL:
  - no test coverage for precedence/grouping behavior.

### K29 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K29 exit criteria

- Expression precedence/grouping scaffold is integrated and test-covered.
- Ready for K30 expression token quoting and escaped-value support.

## K30 - Expression Quoting and Escaped-value Scaffold

Status: done

### K30.1 Quoted predicate values

- Task: allow quoted values in telemetry where predicates.
- PASS:
  - parser accepts quoted values for event-related and success predicates.
  - quoted values support embedded escaped quotes/backslashes.
- FAIL:
  - quoted values are rejected or parsed incorrectly.

### K30.2 Tokenizer quote safety

- Task: make tokenizer quote-aware so grouped expressions keep quoted literals intact.
- PASS:
  - spaces/parentheses inside quoted values do not break token boundaries.
  - unterminated quoted values fail with explicit error.
- FAIL:
  - tokenizer splits quoted values incorrectly.

### K30.3 Normalized expression output

- Task: emit normalized where expression while preserving explicit quoting intent when requested.
- PASS:
  - JSON query `where` output remains deterministic for quoted/unquoted equivalent expressions.
  - explicitly quoted tokens remain quoted in normalized output.
- FAIL:
  - normalized output drops important quote semantics.

### K30.4 Regression coverage

- Task: validate quoted expression behavior in CLI smoke.
- PASS:
  - smoke executes quoted `--where` expression successfully.
  - smoke asserts normalized output for quoted tokens.
- FAIL:
  - no test coverage for quoted expression path.

### K30 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K30 exit criteria

- Quoted/escaped expression values are integrated and test-covered.
- Ready for K31 expression unary-not and richer predicate families.

## K31 - Unary NOT Expression Scaffold

Status: done

### K31.1 Parser support

- Task: add unary `NOT` support in where-expression grammar.
- PASS:
  - parser accepts `NOT` before predicates and grouped expressions.
  - malformed `NOT` placements produce explicit errors.
- FAIL:
  - `NOT` tokens are rejected or misparsed.

### K31.2 Evaluator support

- Task: compile and evaluate unary negation in expression execution.
- PASS:
  - compiled token stream includes unary `NOT` operator.
  - evaluator applies negation correctly with precedence-aware flow.
- FAIL:
  - unary negation has no effect or breaks stack evaluation.

### K31.3 Regression coverage

- Task: validate NOT-expression query in CLI smoke.
- PASS:
  - smoke executes `NOT` where query successfully.
  - smoke asserts normalized query expression contains `NOT`.
- FAIL:
  - no tests for unary-not path.

### K31 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K31 exit criteria

- Unary NOT support is integrated and test-covered.
- Ready for K32 additional predicate families.

## K32 - Additional Predicate Family Scaffold

Status: done

### K32.1 Event suffix predicate

- Task: add event suffix predicate for where expressions.
- PASS:
  - parser accepts `event_suffix==...`.
  - evaluator matches event names by suffix.
- FAIL:
  - no suffix predicate support in parser/evaluator.

### K32.2 Regression coverage

- Task: validate suffix predicate in CLI smoke tests.
- PASS:
  - smoke executes suffix predicate query successfully.
  - smoke asserts normalized query contains `event_suffix` predicate.
- FAIL:
  - no tests for suffix predicate behavior.

### K32 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K32 exit criteria

- Additional suffix predicate support is integrated and test-covered.
- Ready for K33 query plan explainability scaffold.

## K33 - Query Plan Explainability Scaffold

Status: done

### K33.1 Explain mode option

- Task: add telemetry query explain mode for machine-readable diagnostics.
- PASS:
  - `telemetry-dump-mounted` accepts `--explain`.
  - explain mode forces JSON output and includes query-plan metadata.
- FAIL:
  - no explain mode for telemetry query behavior.

### K33.2 Query plan metadata

- Task: surface normalized planner characteristics for current query.
- PASS:
  - JSON includes `query_plan` object with selector/where summary.
  - plan metadata includes compiled where token count.
- FAIL:
  - query diagnostics have no plan-level metadata.

### K33.3 Regression coverage

- Task: validate explain mode in CLI smoke.
- PASS:
  - smoke executes `--explain` successfully.
  - smoke asserts `query_plan` and `where_rpn_tokens` fields.
- FAIL:
  - no tests for explain mode.

### K33 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K33 exit criteria

- Explain mode and query plan metadata are integrated and test-covered.
- Ready for K34 richer detail predicates.

## K34 - Detail Predicate Family Scaffold

Status: done

### K34.1 Detail predicate parser support

- Task: add detail payload predicate for where-expression filtering.
- PASS:
  - parser accepts `detail_contains==...` predicate.
  - normalized where output preserves detail predicate form.
- FAIL:
  - detail predicate not recognized by parser.

### K34.2 Detail predicate evaluator support

- Task: evaluate detail predicate against telemetry event detail field.
- PASS:
  - evaluator reads `detail` JSON field and applies contains match.
  - behavior remains deterministic for missing detail field.
- FAIL:
  - detail predicate is ignored during evaluation.

### K34.3 Regression coverage

- Task: validate detail predicate path in CLI smoke tests.
- PASS:
  - smoke executes detail predicate query successfully.
  - smoke asserts normalized query contains detail predicate.
- FAIL:
  - no tests for detail predicate.

### K34 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K34 exit criteria

- Detail predicate support is integrated and test-covered.
- Ready for K35 advanced comparison predicates.

## K35 - Detail Prefix Predicate Scaffold

Status: done

### K35.1 Detail prefix parser support

- Task: add detail prefix predicate for where expressions.
- PASS:
  - parser accepts `detail_prefix==...`.
  - normalized where output preserves detail-prefix predicate.
- FAIL:
  - parser rejects detail prefix predicate.

### K35.2 Detail prefix evaluator support

- Task: evaluate detail prefix predicate against telemetry detail field.
- PASS:
  - evaluator performs starts-with match on `detail` value.
  - missing detail field yields deterministic false.
- FAIL:
  - detail prefix predicate is ignored or mis-evaluated.

### K35.3 Regression coverage

- Task: validate detail prefix predicate in CLI smoke.
- PASS:
  - smoke executes detail-prefix query successfully.
  - smoke asserts normalized output contains `detail_prefix` predicate.
- FAIL:
  - no tests for detail-prefix predicate.

### K35 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K35 exit criteria

- Detail prefix predicate is integrated and test-covered.
- Ready for K36 detail suffix predicate scaffold.

## K36 - Detail Suffix Predicate Scaffold

Status: done

### K36.1 Detail suffix parser support

- Task: add detail suffix predicate for where expressions.
- PASS:
  - parser accepts `detail_suffix==...`.
  - normalized where output preserves detail-suffix predicate.
- FAIL:
  - parser rejects detail suffix predicate.

### K36.2 Detail suffix evaluator support

- Task: evaluate detail suffix predicate against telemetry detail field.
- PASS:
  - evaluator performs ends-with match on `detail` value.
  - missing detail field yields deterministic false.
- FAIL:
  - detail suffix predicate is ignored or mis-evaluated.

### K36.3 Regression coverage

- Task: validate detail suffix predicate in CLI smoke.
- PASS:
  - smoke executes detail-suffix query successfully.
  - smoke asserts normalized output contains `detail_suffix` predicate.
- FAIL:
  - no tests for detail-suffix predicate.

### K36 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K36 exit criteria

- Detail suffix predicate is integrated and test-covered.
- Ready for K37 case-insensitive event equality predicate scaffold.

## K37 - Case-insensitive Event Equality Scaffold

Status: done

### K37.1 Parser support

- Task: add case-insensitive event equality predicate.
- PASS:
  - parser accepts `event_ieq==...`.
  - normalized where output preserves predicate form.
- FAIL:
  - parser rejects `event_ieq` predicate.

### K37.2 Evaluator support

- Task: evaluate event equality ignoring ASCII case.
- PASS:
  - evaluator compares lowercased event names and predicate value.
  - deterministic behavior for mixed-case inputs.
- FAIL:
  - evaluation remains case-sensitive.

### K37.3 Regression coverage

- Task: validate case-insensitive event-equality predicate in CLI smoke.
- PASS:
  - smoke executes uppercase `event_ieq` query successfully.
  - smoke asserts normalized output contains `event_ieq` predicate.
- FAIL:
  - no tests for `event_ieq` path.

### K37 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K37 exit criteria

- Case-insensitive event equality is integrated and test-covered.
- Ready for K38 case-insensitive event contains scaffold.

## K38 - Case-insensitive Event Contains Scaffold

Status: done

### K38.1 Parser support

- Task: add case-insensitive event contains predicate.
- PASS:
  - parser accepts `event_icontains==...`.
  - normalized where output preserves predicate form.
- FAIL:
  - parser rejects `event_icontains` predicate.

### K38.2 Evaluator support

- Task: evaluate event contains predicate ignoring ASCII case.
- PASS:
  - evaluator compares lowercased event name and needle.
  - deterministic behavior for mixed-case inputs.
- FAIL:
  - contains matching remains case-sensitive.

### K38.3 Regression coverage

- Task: validate case-insensitive event-contains predicate in CLI smoke.
- PASS:
  - smoke executes uppercase `event_icontains` query successfully.
  - smoke asserts normalized output contains `event_icontains` predicate.
- FAIL:
  - no tests for `event_icontains` path.

### K38 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K38 exit criteria

- Case-insensitive event contains is integrated and test-covered.
- Ready for K39 explain-plan operator breakdown metadata.

## K39 - Explain-plan RPN Breakdown Scaffold

Status: done

### K39.1 Explain metadata extension

- Task: extend explain mode with RPN operator breakdown counters.
- PASS:
  - `query_plan` includes `rpn_predicates`, `rpn_not`, `rpn_and`, `rpn_or`.
  - counters are consistent with compiled where token stream.
- FAIL:
  - explain output lacks RPN breakdown counters.

### K39.2 Regression coverage

- Task: validate new explain counters in CLI smoke.
- PASS:
  - smoke asserts `rpn_and` and `rpn_or` fields.
  - existing explain assertions remain green.
- FAIL:
  - no tests for breakdown metadata.

### K39 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K39 exit criteria

- Explain RPN breakdown metadata is integrated and test-covered.
- Ready for K40 normalized query hash metadata.

## K40 - Normalized Query Hash Metadata Scaffold

Status: done

### K40.1 Explain hash metadata

- Task: provide stable fingerprint field for normalized where expression.
- PASS:
  - explain output includes `normalized_where_hash`.
  - hash is derived from normalized expression string.
- FAIL:
  - explain output has no query hash metadata.

### K40.2 Regression coverage

- Task: validate hash metadata in CLI smoke.
- PASS:
  - smoke asserts `normalized_where_hash` field in explain output.
  - existing explain assertions remain green.
- FAIL:
  - no tests for normalized query hash metadata.

### K40 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K40 exit criteria

- Normalized query hash metadata is integrated and test-covered.
- Ready for K41 explain/plan export schema hardening.

## K41 - Event Starts-with Predicate Scaffold

Status: done

### K41.1 Parser support

- Task: add explicit event starts-with predicate.
- PASS:
  - parser accepts `event_starts_with==...`.
  - normalized where output preserves predicate form.
- FAIL:
  - parser rejects starts-with predicate.

### K41.2 Evaluator support

- Task: evaluate starts-with predicate against telemetry event name.
- PASS:
  - evaluator uses starts-with semantics on event name.
  - deterministic behavior for empty/non-empty prefixes.
- FAIL:
  - starts-with predicate is ignored or mis-evaluated.

### K41.3 Regression coverage

- Task: validate starts-with predicate in CLI smoke.
- PASS:
  - smoke executes `event_starts_with` query successfully.
  - smoke asserts normalized output includes `event_starts_with`.
- FAIL:
  - no tests for starts-with predicate.

### K41 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K41 exit criteria

- Event starts-with predicate is integrated and test-covered.
- Ready for K42 event ends-with predicate scaffold.

## K42 - Event Ends-with Predicate Scaffold

Status: done

### K42.1 Parser support

- Task: add explicit event ends-with predicate.
- PASS:
  - parser accepts `event_ends_with==...`.
  - normalized where output preserves predicate form.
- FAIL:
  - parser rejects ends-with predicate.

### K42.2 Evaluator support

- Task: evaluate ends-with predicate against telemetry event name.
- PASS:
  - evaluator uses ends-with semantics on event name.
  - deterministic behavior for empty/non-empty suffix.
- FAIL:
  - ends-with predicate is ignored or mis-evaluated.

### K42.3 Regression coverage

- Task: validate ends-with predicate in CLI smoke.
- PASS:
  - smoke executes `event_ends_with` query successfully.
  - smoke asserts normalized output includes `event_ends_with`.
- FAIL:
  - no tests for ends-with predicate.

### K42 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K42 exit criteria

- Event ends-with predicate is integrated and test-covered.
- Ready for K43 event detail case-insensitive predicate scaffold.

## K43 - Case-insensitive Detail Contains Scaffold

Status: done

### K43.1 Parser support

- Task: add case-insensitive detail contains predicate.
- PASS:
  - parser accepts `detail_icontains==...`.
  - normalized where output preserves predicate form.
- FAIL:
  - parser rejects `detail_icontains` predicate.

### K43.2 Evaluator support

- Task: evaluate detail contains predicate ignoring ASCII case.
- PASS:
  - evaluator compares lowercased detail value and needle.
  - deterministic behavior for mixed-case inputs.
- FAIL:
  - detail contains remains case-sensitive.

### K43.3 Regression coverage

- Task: validate case-insensitive detail-contains predicate in CLI smoke.
- PASS:
  - smoke executes uppercase `detail_icontains` query successfully.
  - smoke asserts normalized output contains `detail_icontains` predicate.
- FAIL:
  - no tests for `detail_icontains` path.

### K43 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K43 exit criteria

- Case-insensitive detail contains is integrated and test-covered.
- Ready for K44 query-plan complexity metadata scaffold.

## K44 - Query-plan Complexity Metadata Scaffold

Status: done

### K44.1 Explain complexity field

- Task: add coarse query-plan complexity classification to explain output.
- PASS:
  - explain output includes `query_plan_complexity`.
  - complexity is emitted as one of `simple`, `moderate`, `complex`.
- FAIL:
  - explain output has no complexity classification.

### K44.2 Regression coverage

- Task: validate complexity field in CLI smoke.
- PASS:
  - smoke asserts `query_plan_complexity` field presence.
  - existing explain assertions remain green.
- FAIL:
  - no tests for complexity metadata.

### K44 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K44 exit criteria

- Query-plan complexity metadata is integrated and test-covered.
- Ready for K45 explain planner score metadata scaffold.

## K45 - Query-plan Score Metadata Scaffold

Status: done

### K45.1 Explain score field

- Task: add numeric query-plan score in explain output.
- PASS:
  - explain output includes `query_plan_score`.
  - score is derived from RPN operator/predicate counts with deterministic formula.
- FAIL:
  - explain output has no score metadata.

### K45.2 Regression coverage

- Task: validate score field in CLI smoke.
- PASS:
  - smoke asserts `query_plan_score` field presence.
  - existing explain assertions remain green.
- FAIL:
  - no tests for score metadata.

### K45 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K45 exit criteria

- Query-plan score metadata is integrated and test-covered.
- Ready for K46 explain predicate-kind counters scaffold.

## K46 - Explain Predicate-kind Counter Scaffold

Status: done

### K46.1 Predicate-kind counters

- Task: extend explain output with predicate category counters.
- PASS:
  - `query_plan` includes `pred_event`, `pred_detail`, and `pred_success`.
  - counters reflect compiled predicate kinds deterministically.
- FAIL:
  - explain output lacks predicate-kind counters.

### K46.2 Regression coverage

- Task: validate predicate-kind counters in CLI smoke.
- PASS:
  - smoke asserts `pred_event`, `pred_detail`, and `pred_success` fields.
  - existing explain assertions remain green.
- FAIL:
  - no tests for predicate-kind counters.

### K46 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K46 exit criteria

- Predicate-kind counters are integrated and test-covered.
- Ready for K47 explain/filter scan-efficiency metadata scaffold.

## K47 - Explain Scan-efficiency Metadata Scaffold

Status: done

### K47.1 Scan-efficiency fields

- Task: expose scan/match efficiency metrics in explain output.
- PASS:
  - `query_plan` includes `scanned_entries`, `matched_entries`, and `scan_match_ratio`.
  - ratio is deterministic and safe for zero-scan cases.
- FAIL:
  - explain output lacks scan-efficiency metadata.

### K47.2 Regression coverage

- Task: validate scan-efficiency metadata in CLI smoke.
- PASS:
  - smoke asserts `scan_match_ratio` field presence.
  - existing explain assertions remain green.
- FAIL:
  - no tests for scan-efficiency metadata.

### K47 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K47 exit criteria

- Scan-efficiency metadata is integrated and test-covered.
- Ready for K48 where-feature bitmask metadata scaffold.

## K48 - Where-feature Bitmask Metadata Scaffold

Status: in progress

### K48.1 Feature bitmask field

- Task: add compact where-feature bitmask in explain output.
- PASS:
  - explain output includes `where_feature_mask`.
  - bitmask encodes logical operators and predicate family usage.
- FAIL:
  - explain output has no where-feature bitmask.

### K48.2 Regression coverage

- Task: validate bitmask metadata in CLI smoke.
- PASS:
  - smoke asserts `where_feature_mask` field presence.
  - existing explain assertions remain green.
- FAIL:
  - no tests for where-feature bitmask metadata.

### K48 verification commands

- Build:
  - `cmake -S . -B build`
  - `cmake --build build --config Release`
- Test:
  - `ctest --test-dir build --output-on-failure`

### K48 exit criteria

- Where-feature bitmask metadata is integrated and test-covered.
- Ready for K49 explain output ordering/stability scaffold.

Release gate reference:
- `V1_RELEASE_CHECKLIST.md`
