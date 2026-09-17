# K1 Driver Lab Setup Guide

Goal: prepare a safe, repeatable environment for kernel driver development and debugging.

## 1) Safety policy (mandatory)

- Never load experimental kernel drivers on the primary workstation.
- Use an isolated VM only.
- Create a VM snapshot before every major driver milestone.
- Keep symbols/logs from each test session.

## 2) Host and VM prerequisites

- Host machine with virtualization enabled (Hyper-V/VMware/VirtualBox).
- Windows VM (recommended: Windows 11 x64) with admin access.
- Stable network between host and VM (for symbols/debug tooling).

## 3) Toolchain installation in VM

- Visual Studio (Desktop development with C++).
- Windows Driver Kit (WDK) matching VS version.
- WinDbg Preview.
- Optional: Sysinternals tools for diagnostics.

Quick self-check command (host or VM):

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\k1-lab-selfcheck.ps1
```

## 4) Kernel debugging baseline

- Enable kernel debugging mode for VM (according to hypervisor setup).
- Verify WinDbg can attach to target VM.
- Validate symbol path is configured and symbols resolve.

Expected evidence:
- Screenshot/log of active kernel debug session.
- WinDbg command output proving target attached.

## 5) Driver Verifier baseline

- Enable Driver Verifier for test-signed target driver only.
- Start with conservative checks, then increase strictness in later stages.
- Record verifier configuration used.

Expected evidence:
- Verifier configuration export/screenshot.

## 6) Test-signing mode (dev only)

- Enable test signing for development builds in VM only.
- Reboot VM and confirm mode is active.

Expected evidence:
- Command output showing test-signing state.

## 7) Artifact locations

For each session archive:
- build logs
- install/load logs
- WinDbg logs
- verifier config
- crash dump (if any)

Recommended structure:

```text
artifacts/kdrv/<date-time>/
  build.log
  install.log
  windbg.log
  verifier.txt
  dumps/
```

## 8) K1 completion criteria

K1 is complete when:
- VM lab is reproducible from scratch.
- WinDbg kernel attach verified.
- Driver Verifier baseline verified.
- Snapshot/rollback procedure documented and tested.
- `K1_LAB_VALIDATION.md` completed with PASS for mandatory checks.
