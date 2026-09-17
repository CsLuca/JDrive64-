# JDrive64

JDrive64 is a Windows virtual filesystem project that mounts a Commodore 1541 `.D64` floppy image as a drive.

Current implementation status is an advanced core/parser stage with a read-only mount facade prepared for WinFsp wiring.

## Scope for v1.0

- `.D64` only.
- Read-only only.
- CLI-driven mount and unmount.

See `ROADMAP_BASELINE.md` for milestones and final Definition of Done.
Execution tracking is in `ROADMAP_EXECUTABLE.md`.
Explorer host validation checklist is in `A8_EXPLORER_CHECKLIST.md`.
Release gate checklist is in `V1_RELEASE_CHECKLIST.md`.
Evidence template is in `A8_EVIDENCE_TEMPLATE.md`.
Kernel lab setup docs are in `K1_DRIVER_LAB_SETUP.md` and `K1_LAB_VALIDATION.md`.

## Versioning

- This project uses Semantic Versioning.
- Current version is tracked in `VERSION`.
- Release notes are tracked in `CHANGELOG.md`.
- Git tags follow the format `vMAJOR.MINOR.PATCH`.

## Project Layout

- `include/jdrive64/` public headers.
- `src/` core logic and CLI.
- `tests/` core tests.

## Commands

- `jdrive64 version`
- `jdrive64 info <image.d64>`
- `jdrive64 ls <image.d64>`
- `jdrive64 extract <image.d64> [output_dir]`
- `jdrive64 mount <image.d64> <drive_letter:>`
- `jdrive64 mount <image.d64> <drive_letter:> --backend <winfsp|kdrv>`
- `jdrive64 mounts`
- `jdrive64 unmount <drive_letter:>`
- `jdrive64 dir-mounted <drive_letter:>`
- `jdrive64 read-mounted <drive_letter:> <name.ext>`
- `jdrive64 volume-mounted <drive_letter:>`
- `jdrive64 stats-mounted <drive_letter:>`
- `jdrive64 check-mounted <drive_letter:>`
- `jdrive64 winfsp-preflight <image.d64> <drive_letter:>`

## Build

Preferred with CMake:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Run tests:

```powershell
ctest --test-dir build --output-on-failure
```

Step 2 focused tests:

```powershell
ctest --test-dir build -R jdrive64_step2_tests --output-on-failure
ctest --test-dir build -R jdrive64_cli_smoke_tests --output-on-failure
```

Release readiness helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\release-readiness.ps1
```

K1 lab self-check helper:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\k1-lab-selfcheck.ps1
```

## Notes on Current WinFsp Status

- `WinFspFilesystem` exists as a read-only integration scaffold.
- Read/write policies and catalog/file read path are in place.
- Callback-like methods are now available for integration wiring: `GetVolumeInfo`, `GetFileInfo`, `Open`, `Read`, `Close`, `ReadDirectory`.
- Mutation-oriented methods are hard-blocked in read-only mode and return `ACCESS_DENIED` (`Create`, `Write`, `Delete`, `Rename`, `SetFileSize`, `SetFileAttributes`).

## Kernel Backend Status

- `kdrv` backend is scaffolded for K2 and currently returns a deterministic not-implemented mount error.
- Driver/service binaries (`jdrive64k.sys`, `jdrive64ksvc.exe`) are not wired yet.
- Drive-letter assignment path now includes K3 mount-manager scaffold with deterministic not-implemented behavior.
- K4 read-only catalog/open/read scaffolding is available internally for kdrv request path shaping.
- K5 IOCTL-like bridge scaffolding (`KernelUserBridge`) now maps opcode requests to K4 read-only handlers.
- K6 IPC channel scaffold (`KernelIpcChannel`) is wired in controller lifecycle for directory request flow.
- K7 transport scaffold (`KernelTransport`) introduces loopback/device modes and placeholder device-channel connect.
- K8 frame scaffold extends device mode with deterministic request encoding and response-frame parsing contracts.
- K9 transport scaffold adds injectable DeviceIo API and real device send pipeline stages (open/ioctl/parse/close).
- K10 handshake scaffold adds protocol-version and capability negotiation before non-handshake device requests.
- K11 compatibility scaffold extends handshake with protocol window support and feature-flag negotiation policy.
- K12 policy scaffold adds opcode-level request gating from negotiated capabilities/features with explicit denial reasons.
- K13 rollout scaffold introduces strict vs best-effort feature policy and granular handle-operation gating.
- K14 diagnostics scaffold surfaces backend negotiation/policy state via backend diagnostics API and CLI `backend-diag`.
- K15 persistence scaffold stores backend/diagnostics snapshot in mount-state and exposes `backend-diag-mounted`.
- K16 export scaffold adds machine-readable diagnostics output (`--json`) for both live and mounted backend inspection.
- K17 telemetry scaffold adds structured transport events for connect/send/disconnect success/failure stages.
- K18 persistence scaffold adds optional JSONL telemetry sink via `JDRIVE64_TELEMETRY_JSONL`.
- K19 management scaffold adds telemetry JSONL rotation plus mounted telemetry dump/clear commands.
- K20 retention scaffold adds multi-file telemetry rotation and mounted telemetry index listing.
- K21 query scaffold adds telemetry dump filters (`--event`, `--success`, `--tail`) for mounted analysis.
- K22 query-output scaffold adds `telemetry-dump-mounted --json` plus pagination (`--offset`, `--limit`).
- K23 cross-file query scaffold merges telemetry search across base+rotated files with env-tuned retention.
- K24 aggregation scaffold adds `telemetry-stats-mounted` with per-event counts/success rates (text + json).
- K25 query DSL scaffold expands telemetry filters (`--exclude-event`, `--event-prefix`, `--event-contains`) and adds export bundle mode (`telemetry-dump-mounted --bundle`).
- K26 selector/schema scaffold adds selector semantics (`--selector-mode all|any`) and JSON schema metadata (`schema_version`, `query`) for telemetry exports.
- K27 normalization/schema-policy scaffold normalizes query filters for deterministic exports and adds explicit schema policy metadata (`schema_policy`).
- K28 expression scaffold adds `--where` query expression parsing (`AND`/`OR`, event/success predicates) with normalized expression export in telemetry JSON query metadata.

## Domain Layer (Step 3)

- `DiskImageSession` is the shared domain entry point for image open, BAM/catalog access, and file reads.
- CLI (`ls`, `extract`) and WinFsp facade reuse the same domain model to avoid duplicated parsing logic.

## Cache Metrics (Step 6)

- Runtime cache telemetry is exposed through `WinFspFilesystem::GetRuntimeStats()` and `GetRuntimeStatsText()`.
- Metrics include sector/file cache hit/miss, cache occupancy, hit rate, read ops, and bytes served.

## Cache Policy (Step 8)

- Runtime cache policy can be tuned with `WinFspFilesystem::ConfigureCaches(sectorCap, fileCap, fileMaxItemSize)`.
- File cache stores only items up to `fileMaxItemSize` bytes (small/medium files), while larger files bypass file-cache insertion.
- Runtime telemetry now includes average read latency (`avg_read_latency_us`) and throughput (`throughput_bytes_per_sec`).

## Mount/Unmount Robustness (Step 7)

- `mount` persists a versioned mount-state file with mount point, absolute image path, and indexed file list.
- `unmount` validates mount-state consistency before removal.
- `dir-mounted` and `read-mounted` re-validate mount-state and read directly from the current image/session.

## Read-Only Error Contract (Step 9)

- `WinFspFilesystem` now tracks normalized status with `LastStatus()`, `LastWin32Error()`, and `LastNtStatus()`.
- Mutation operations return `kAccessDenied` with Win32 `ERROR_ACCESS_DENIED` and NTSTATUS `STATUS_ACCESS_DENIED`.
- Read/open/info paths map failures to consistent status families (`kNotMounted`, `kFileNotFound`, `kInvalidHandle`, `kInvalidParameter`, `kIoError`).

## Volume Metadata (Step 11)

- `WinFspFilesystem::VolumeInfo` now exposes block and byte dimensions:
  - `block_size_bytes`, `capacity_blocks`, `free_blocks`, `used_blocks`
  - `capacity_bytes`, `free_bytes`, `used_bytes`
- CLI `info` now prints volume-oriented fields (`Label`, `FileSystem`, `Capacity`, `Used`, `Free`) in both blocks and bytes.

## D64 Write Editor (Step 10)

- Experimental write path is available through CLI commands:
  - `jdrive64 write-add <image.d64> <host_file> <name.ext>`
  - `jdrive64 write-del <image.d64> <name.ext>`
  - `jdrive64 write-ren <image.d64> <old.ext> <new.ext>`
- The editor updates BAM, directory entries, and file chains directly inside `.d64`.

### Write Hardening

- Write operations use an in-memory transaction snapshot and rollback on failure.
- BAM consistency is verified and corrected after write operations.
- Tests include rollback validation for failed write-add scenarios.

## Milestones

- `v0.2-core-stable`: core parser + tests.
- `v0.3-winfsp-ro`: minimal read-only WinFsp integration.
- `v1.0`: hardened read-only `.D64` mount experience.
