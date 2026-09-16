# JDrive64 Baseline and Milestones

## Step 0 - Scope Freeze (v1.0)

- Supported image format: `.D64` only.
- Filesystem mode: read-only only.
- Mount flow: CLI only (`mount` and `unmount` commands).
- Platform target: Windows with WinFsp integration.

## Milestones

### v0.2-core-stable

- Stable D64 parsing (`D64Reader`, `BAMReader`, `DirectoryReader`, `FileChainReader`).
- PETSCII conversion good enough for reliable file naming.
- CLI commands: `info`, `ls`, `extract`.
- Unit tests for core D64 logic and basic cache behavior.

### v0.3-winfsp-ro

- WinFsp read-only minimal integration.
- CLI commands: `mount`, `unmount`.
- Windows Explorer can list directory and read files from mounted drive.
- Write operations are explicitly denied.

### v1.0

- Read-only D64 mount is stable for day-to-day browsing/extraction.
- Basic sector and file cache integrated.
- Hardening complete: robust errors, clean unmount, clear release checklist.

## Definition of Done (v1.0)

- Mounts `.D64` in Explorer with `jdrive64 mount <image.d64> <drive_letter:>`.
- Reads directory and file content correctly (`PRG`, `SEQ`, `USR`, `REL`).
- Rejects write, rename, delete with access denied semantics.
- Unmounts cleanly with `jdrive64 unmount <drive_letter:>`.
- Core tests pass and release checklist is complete.
