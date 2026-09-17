# Changelog

All notable changes to this project are documented in this file.

The format is based on Keep a Changelog and the project uses Semantic Versioning.

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
