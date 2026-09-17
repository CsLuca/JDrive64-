## A8 Validation Evidence (Automated kdrv Session)

## Environment

- Date: 2026-09-17
- Timestamp: 2026-09-17 22:58:03
- Operator: LBiondi
- Machine: CSW074
- Windows version: Windows-11-10.0.26200-SP0
- JDrive64 executable: `C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe`
- JDrive64 commit: `895b8d2`
- Test image path: `C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64`
- Drive letter used: `R:`

## Command Transcript

```powershell
$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" mount C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64 R:
Mounted C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64 on R: (read-only, backend=kdrv)
Kernel backend scaffold active (K7 transport loopback mode)
EXIT=0

$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" backend-diag-mounted R:
Mount: R:
Image: C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64
Backend: kdrv
Backend=kdrv
ServiceRunning=yes
TransportMode=loopback
FeaturePolicy=strict
HandshakeComplete=yes
NegotiatedProtocol=1
NegotiatedCapabilities=0x7
NegotiatedFeatures=0x7
TelemetrySink=disabled
EXIT=0

$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" mounts
R: -> C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64
EXIT=0

$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" unmount R:
Unmounted R:
EXIT=0

$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" mount C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64 R:
Mounted C:\Users\LBiondi\AppData\Local\Temp\opencode\Jemu64\roms\tsuit215\Source1.d64 on R: (read-only, backend=kdrv)
Kernel backend scaffold active (K7 transport loopback mode)
EXIT=0

$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" unmount R:
Unmounted R:
EXIT=0

$ "C:\Users\LBiondi\OneDrive - centrosoftware.com\Documenti\Default Project\JDrive64\build\jdrive64.exe" mounts
EXIT=0
```

## Automated Checks

- All command exits are zero: PASS
- Diagnostics report `Backend: kdrv`: PASS
- `mounts` contains `R:` right after mount: PASS
- Final `mounts` does not contain `R:` after unmount: PASS

## Manual Explorer Checks (still required for A8 closure)

- Mount visibility in Explorer: PENDING
- Read behavior (open/copy/seek): PENDING
- Denied write policy: PENDING
- Required screenshots captured: PENDING

## Final Verdict

- Command-level A8 automation: PASS
- A8 completed on this host: NO (manual Explorer evidence still required)
