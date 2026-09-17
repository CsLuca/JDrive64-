# A8 Validation Evidence Template

Fill this template after running `A8_EXPLORER_CHECKLIST.md` on a WinFsp-enabled host.

## Environment

- Date:
- Operator:
- Machine:
- Windows version:
- WinFsp version:
- JDrive64 commit:
- Test image path:
- Drive letter used:

## Command Transcript

Paste full terminal transcript for:

- `jdrive64 winfsp-preflight <image.d64> <drive_letter:>`
- `jdrive64 mount <image.d64> <drive_letter:>`
- `jdrive64 unmount <drive_letter:>`
- `jdrive64 mounts` (after final unmount)

## Checklist Results

- Runtime preflight: PASS / FAIL
- Mount visibility in Explorer: PASS / FAIL
- Read behavior: PASS / FAIL
- Denied write policy: PASS / FAIL
- Unmount/remount stability: PASS / FAIL

## Evidence Files

- Screenshot: mounted drive visible
- Screenshot: denied write operation
- Screenshot: drive absent after unmount

## Notes / Anomalies

-

## Final Verdict

- A8 completed on this host: YES / NO
- Blocking issues (if any):
