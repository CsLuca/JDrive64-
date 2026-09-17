# Changelog

All notable changes to this project are documented in this file.

The format is based on Keep a Changelog and the project uses Semantic Versioning.

## [1.0.0] - 2026-09-17

### Added
- WinFsp runtime layering with dedicated runtime, adapter, callback bridge, native bridge, and provider abstraction.
- Default native provider bootstrap with WinFsp DLL loading and symbol preflight.
- Default provider lifecycle integration for `FspFileSystemCreate`, mount-point set, dispatcher start/stop, and delete paths.
- Mounted inspection CLI commands: `mounts`, `volume-mounted`, `stats-mounted`, `check-mounted`.
- WinFsp diagnostic CLI command: `winfsp-preflight <image.d64> <drive_letter:>`.
- Formal release and validation artifacts:
  - `ROADMAP_EXECUTABLE.md`
  - `A8_EXPLORER_CHECKLIST.md`
  - `A8_EVIDENCE_TEMPLATE.md`
  - `V1_RELEASE_CHECKLIST.md`
  - `scripts/release-readiness.ps1`
  - GitHub Actions CI: `.github/workflows/ci-windows-ucrt64.yml`

### Changed
- CLI `info` output now uses volume-oriented reporting (label/filesystem/blocks/bytes).
- Mount-state handling is now consistent and Windows-safe (`X.state`).
- Runtime and adapter lifecycle paths include transactional rollback behavior on initialization failures.

### Fixed
- BAM bitmap/test corpus consistency issues impacting write-path tests.
- CLI smoke reliability in UCRT64 environments via explicit runtime PATH wrapping.
- Read-only error-contract consistency across facade status/Win32/NTSTATUS paths.

## [0.4.0] - 2026-09-17

### Added
- Experimental D64 write editor commands: `write-add`, `write-del`, `write-ren`.
- Write hardening with transaction rollback and BAM consistency verification.
- Rich volume metadata in `VolumeInfo` (block and byte dimensions).
- Mounted inspection commands: `volume-mounted`, `stats-mounted`, `mounts`, `check-mounted`.
- Runtime cache telemetry and cache policy tuning endpoints.

### Changed
- `info` CLI output now presents volume-style fields (`Label`, `FileSystem`, `Capacity`, `Used`, `Free`).
- Mounted command internals now share a common mount-state validation/bootstrap path.

### Fixed
- Mount-state filename compatibility on Windows (`X.state` instead of `X:.state`).
- Golden D64 test BAM bitmap initialization for write-path correctness.
- CLI smoke runner reliability under MSYS2 UCRT64 by forcing runtime PATH.
