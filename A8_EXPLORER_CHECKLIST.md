# A8 Explorer Validation Checklist

Use this checklist on a Windows host to close Step A8 using the default `kdrv` backend path.

## Preconditions

- `jdrive64.exe` is built from current `main`.
- At least one valid `.d64` image is available.
- Chosen mount point (for example `R:`) is not currently in use.

## 1) kdrv mounted diagnostics preflight

Command:

```powershell
jdrive64 mount <image.d64> R:
jdrive64 backend-diag-mounted R:
```

PASS:
- Exit code is `0`.
- Diagnostics output contains `Backend: kdrv`.

FAIL:
- Non-zero exit code.
- Diagnostics output does not report `kdrv` backend.

## 2) Mount visibility in Explorer

Command:

```powershell
jdrive64 mount <image.d64> R:
```

PASS:
- Exit code is `0`.
- Drive `R:` appears in Windows Explorer.
- Opening `R:` shows the disk directory entries.

FAIL:
- Drive does not appear.
- Explorer cannot open directory listing.

## 3) Read behavior

Manual actions:

- Open at least 3 files from Explorer.
- Copy at least 2 files from `R:` to a local folder.
- Open one larger file and scroll/read through content.

PASS:
- Files open successfully.
- Copy-out succeeds with matching content length.
- No crash/hang in Explorer or process.

FAIL:
- Read errors for valid files.
- Explorer freeze/crash.

## 4) Denied write policy

Manual actions in Explorer on `R:`:

- Create new file/folder.
- Rename existing file.
- Delete existing file.
- Paste file into `R:`.

PASS:
- All mutation attempts are denied.
- User-visible behavior is consistent with access denied.

FAIL:
- Any mutation operation succeeds.
- Inconsistent or misleading errors.

## 5) Unmount and remount stability

Commands:

```powershell
jdrive64 unmount R:
jdrive64 mount <image.d64> R:
jdrive64 unmount R:
```

PASS:
- Every command exits `0` in the valid sequence.
- Drive disappears after unmount and reappears after remount.
- No stale mount-state remains (`jdrive64 mounts` does not list `R:` after final unmount).

FAIL:
- Unmount fails with mount still active.
- Remount fails in clean state.
- Stale state blocks future mount.

## 6) Evidence capture (required)

- Save command transcript for all commands above.
- Capture screenshots:
  - Explorer showing mounted drive.
  - Access denied example on write attempt.
  - Explorer after unmount (drive absent).
- Record machine details:
  - Windows version.
  - WinFsp version.
  - JDrive64 commit hash.

## A8 Closure Rule

Step A8 is considered done only when all sections pass on at least one target host using the default `kdrv` backend and evidence is archived.

Quick helper:
- `A8_QUICK_RUNBOOK_KDRV.md`

## Latest status snapshot (2026-09-17)

- Evidence file: `A8_EVIDENCE_2026-09-17_LOCAL.md`
- Current local session result:
  - kdrv diagnostics preflight: PASS (`mount` and `backend-diag-mounted` report kdrv)
  - CLI mount/unmount stability: PASS
  - Explorer manual checks: PENDING
- Closure status: A8 remains open until manual Explorer checks and screenshots are completed.
