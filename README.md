# JDrive64

JDrive64 is a Windows virtual filesystem project that mounts a Commodore 1541 `.D64` floppy image as a drive.

Current implementation status is an advanced core/parser stage with a read-only mount facade prepared for WinFsp wiring.

## Scope for v1.0

- `.D64` only.
- Read-only only.
- CLI-driven mount and unmount.

See `ROADMAP_BASELINE.md` for milestones and final Definition of Done.

## Project Layout

- `include/jdrive64/` public headers.
- `src/` core logic and CLI.
- `tests/` core tests.

## Commands

- `jdrive64 info <image.d64>`
- `jdrive64 ls <image.d64>`
- `jdrive64 extract <image.d64> [output_dir]`
- `jdrive64 mount <image.d64> <drive_letter:>`
- `jdrive64 unmount <drive_letter:>`
- `jdrive64 dir-mounted <drive_letter:>`
- `jdrive64 read-mounted <drive_letter:> <name.ext>`

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

## Notes on Current WinFsp Status

- `WinFspFilesystem` exists as a read-only integration scaffold.
- Read/write policies and catalog/file read path are in place.
- Callback-like methods are now available for integration wiring: `GetVolumeInfo`, `GetFileInfo`, `Open`, `Read`, `Close`, `ReadDirectory`.
- Mutation-oriented methods are hard-blocked in read-only mode and return `ACCESS_DENIED` (`Create`, `Write`, `Delete`, `Rename`, `SetFileSize`, `SetFileAttributes`).

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

## Milestones

- `v0.2-core-stable`: core parser + tests.
- `v0.3-winfsp-ro`: minimal read-only WinFsp integration.
- `v1.0`: hardened read-only `.D64` mount experience.
