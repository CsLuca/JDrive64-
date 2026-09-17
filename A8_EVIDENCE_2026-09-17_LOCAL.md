## A8 Validation Evidence (Local Session)

Reference template: `A8_EVIDENCE_TEMPLATE.md`

## Environment

- Date: 2026-09-17
- Operator: OpenCode automation session (local host)
- Machine: local workstation (non-VM evidence session)
- Windows version: Microsoft Windows 11 Business
- JDrive64 commit: `f449ac7`
- Test image path: `C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64`
- Drive letter used: `R:`

## Command Transcript

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + $env:PATH
$exe = "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe"

& $exe mount "C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64" R:
# Mounted ... on R: (read-only, backend=kdrv)
# MOUNT_EXIT=0

& $exe backend-diag-mounted R:
# Backend: kdrv
# ...diagnostics...

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

- kdrv diagnostics preflight: PASS (`mount` and `backend-diag-mounted` succeed with `kdrv`)
- Mount visibility in Explorer: PENDING (manual Explorer check not executed in this session)
- Read behavior: PENDING (manual Explorer check not executed in this session)
- Denied write policy: PENDING (manual Explorer check not executed in this session)
- Unmount/remount stability: PASS (CLI mount/unmount cycle successful)

## Evidence Files

- Screenshot: mounted drive visible - PENDING
- Screenshot: denied write operation - PENDING
- Screenshot: drive absent after unmount - PENDING

## Notes / Anomalies

- This session confirms CLI mount state behavior, kdrv diagnostics surface, and unmount/remount flow.
- A8 closure still requires manual Explorer validation and screenshots.

## Final Verdict

- A8 completed on this host: NO
- Blocking issues:
  - Required Explorer screenshots and manual read/write-denial checks are not yet collected.
