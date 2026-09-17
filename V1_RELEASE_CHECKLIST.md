# v1.0 Release Hardening Checklist

Use this checklist before tagging `v1.0.0`.

## Build and CI

- [ ] GitHub Actions `CI Windows UCRT64` green on latest `main`.
- [ ] Local build succeeds (`cmake -S . -B build` + `cmake --build build`).
- [ ] Local tests pass (`ctest --test-dir build --output-on-failure`).

## kdrv runtime readiness

- [ ] `jdrive64 mount <image.d64> <drive_letter:>` succeeds with default backend (`kdrv`).
- [ ] `jdrive64 backend-diag-mounted <drive_letter:>` reports `Backend: kdrv`.
- [ ] kdrv mount/unmount cycle works repeatedly without stale state.

## Explorer end-to-end validation

- [ ] `A8_EXPLORER_CHECKLIST.md` executed and archived with evidence (kdrv path).
- [ ] Evidence recorded using `A8_EVIDENCE_TEMPLATE.md`.
- [ ] Read flows stable in Explorer (open/copy/seek).
- [ ] Write attempts denied consistently.
- [ ] Unmount/remount cycle stable.

## Scope and policy

- [ ] `.D64` read-only mount scope confirmed.
- [ ] Write path remains marked experimental (or promotion decision explicitly documented).
- [ ] Error contract (`LastStatus`, Win32, NTSTATUS) unchanged or documented.

## Release metadata

- [ ] `VERSION` updated to `1.0.0`.
- [ ] `CHANGELOG.md` includes `1.0.0` section with key changes and known limits.
- [ ] `README.md` reflects current release behavior and prerequisites.

## Git release steps

- [ ] `powershell -ExecutionPolicy Bypass -File .\scripts\release-readiness.ps1` passes.
- [ ] `main` clean (`git status` clean).
- [ ] Tag created: `git tag -a v1.0.0 -m "Release v1.0.0"`.
- [ ] Tag pushed: `git push origin v1.0.0`.
