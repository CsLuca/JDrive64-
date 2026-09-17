# A8 Quick Runbook (kdrv-first)

Use this quick sequence to close A8 evidence on a target Windows host.

## 0) Inputs

- Replace `<IMAGE>` with a valid `.d64` file path.
- Use drive letter `R:` (or any free letter).

Optional automation script:

```powershell
python .\scripts\a8_kdrv_automation.py --image "<IMAGE>" --drive R:
```

This generates `A8_EVIDENCE_YYYY-MM-DD_AUTO.md` with command transcript and pass/fail summary.

## 1) Command transcript (copy/paste as-is)

```powershell
$exe = ".\build\jdrive64.exe"
$img = "<IMAGE>"

& $exe mount $img R:
& $exe backend-diag-mounted R:
& $exe mounts

# Manual Explorer checks while R: is mounted:
# - Open at least 3 files
# - Copy at least 2 files to local disk
# - Attempt create/rename/delete/paste on R: (must be denied)

& $exe unmount R:
& $exe mount $img R:
& $exe unmount R:
& $exe mounts
```

## 2) Required screenshots

1. Explorer shows mounted `R:` drive.
2. Access denied on a write attempt in `R:`.
3. Explorer after final unmount (drive absent).

## 3) Fill evidence file

- Copy transcript and outcomes into `A8_EVIDENCE_TEMPLATE.md` (or a dated evidence file).
- Record:
  - Windows version
  - JDrive64 commit hash (`git rev-parse --short HEAD`)
  - Test image path and drive letter

## 4) PASS criteria to mark A8 done

- `mount` and `backend-diag-mounted` return exit code `0` and diagnostics report `Backend: kdrv`.
- Explorer read flows are stable.
- All write attempts are denied.
- Unmount/remount cycle is stable and `jdrive64 mounts` is empty after final unmount.
