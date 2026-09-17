# K1 Lab Validation Record

Use this file to record PASS/FAIL evidence for K1.

## Session metadata

- Date: 2026-09-17
- Operator: pending
- VM name: pending
- Windows build: Microsoft Windows 11 Business
- Visual Studio version: detected (local install path found)
- WDK version: detected (Windows Kits root found)
- WinDbg version: detected (`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\windbg.exe`)

## Automated self-check snapshot

Command used:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\k1-lab-selfcheck.ps1
```

Observed result summary:

- Hypervisor: PASS
- Visual Studio: PASS
- Windows Kits/WDK headers: PASS
- WinDbg presence: PASS
- Driver Verifier command/query: PASS
- Test-signing state: WARN (admin privileges required to query `bcdedit` in this context)

## Checks

1) VM isolation and snapshot
- PASS / FAIL: PENDING
- Evidence: requires manual VM validation and screenshot/log

2) Toolchain installed (VS + WDK)
- PASS / FAIL: PASS (local self-check)
- Evidence: self-check output reports VS and KitsRoot10 present

3) WinDbg kernel attach works
- PASS / FAIL: PENDING
- Evidence: requires active kernel attach evidence from VM session

4) Symbols resolve correctly
- PASS / FAIL: PENDING
- Evidence: requires WinDbg symbol resolution proof in VM

5) Driver Verifier baseline configured
- PASS / FAIL: PASS (partial)
- Evidence: `verifier /query` executed; baseline config capture still required

6) Test-signing mode active (dev VM only)
- PASS / FAIL: PENDING
- Evidence: rerun self-check/admin `bcdedit` in VM and record output

7) Snapshot rollback tested
- PASS / FAIL: PENDING
- Evidence: manual rollback test required in VM

## Final K1 verdict

- K1 complete: NO
- Blocking issues:
  - Missing VM-only evidence for kernel attach, symbols, and snapshot rollback.
  - Test-signing state not yet confirmed from elevated VM session.
