## A8 Validation Evidence (Local Session)

Reference template: `A8_EVIDENCE_TEMPLATE.md`

## Environment

- Date: 2026-09-17
- Operator: OpenCode automation session (local host)
- Machine: local workstation (non-VM evidence session)
- Windows version: Microsoft Windows 11 Business
- WinFsp version: not verified in this session
- JDrive64 commit: `e8f9c03`
- Test image path: `C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64`
- Drive letter used: `R:`

## Command Transcript

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
$exe = "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe"

& $exe winfsp-preflight "C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64" R:
# Error: WinFsp runtime support is disabled (build with JDRIVE64_ENABLE_WINFSP)
# PREFLIGHT_EXIT=1

& $exe mount "C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64" R:
# Mounted ... on R: (read-only, backend=winfsp)
# MOUNT_EXIT=0

& $exe mounts
# R: -> C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64
# MOUNTS_AFTER_MOUNT_EXIT=0

& $exe unmount R:
# Unmounted R:
# UNMOUNT_EXIT=0

& $exe mounts
# (no mounted drives listed)
# MOUNTS_AFTER_UNMOUNT_EXIT=0
```

## Checklist Results

- Runtime preflight: FAIL (expected in this build: WinFsp runtime support disabled)
- Mount visibility in Explorer: PENDING (manual Explorer check not executed in this session)
- Read behavior: PENDING (manual Explorer check not executed in this session)
- Denied write policy: PENDING (manual Explorer check not executed in this session)
- Unmount/remount stability: PASS (CLI mount/unmount cycle successful)

## Evidence Files

- Screenshot: mounted drive visible - PENDING
- Screenshot: denied write operation - PENDING
- Screenshot: drive absent after unmount - PENDING

## Notes / Anomalies

- This session confirms CLI mount state behavior and unmount/remount flow.
- A8 closure still requires execution on a WinFsp-enabled host/build where `winfsp-preflight` returns success.

## Final Verdict

- A8 completed on this host: NO
- Blocking issues:
  - `winfsp-preflight` fails because current build has WinFsp runtime support disabled.
  - Required Explorer screenshots and manual read/write-denial checks are not yet collected.
