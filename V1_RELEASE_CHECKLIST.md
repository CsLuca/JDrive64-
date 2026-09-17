# v1.0 Release Hardening Checklist

Use this checklist before tagging `v1.0.0`.

## Build and CI

- [ ] GitHub Actions `CI Windows UCRT64` green on latest `main`.
- [ ] Local build succeeds (`cmake -S . -B build` + `cmake --build build`).
- [ ] Local tests pass (`ctest --test-dir build --output-on-failure`).

## WinFsp runtime readiness

- [ ] `jdrive64 winfsp-preflight <image.d64> <drive_letter:>` passes on a WinFsp host.
- [ ] No missing symbol or DLL errors on target host.
- [ ] Runtime start/stop works repeatedly without stale state.

## Explorer end-to-end validation

- [ ] `A8_EXPLORER_CHECKLIST.md` executed and archived with evidence.
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

- [ ] `main` clean (`git status` clean).
- [ ] Tag created: `git tag -a v1.0.0 -m "Release v1.0.0"`.
- [ ] Tag pushed: `git push origin v1.0.0`.
