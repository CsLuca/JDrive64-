#!/usr/bin/env python3
import argparse
import datetime as dt
import os
import platform
import subprocess
import sys
from pathlib import Path


def run_command(args, env=None):
    completed = subprocess.run(args, capture_output=True, text=True, env=env)
    return {
        "cmd": " ".join(f'"{a}"' if " " in a else a for a in args),
        "exit": completed.returncode,
        "stdout": completed.stdout.strip(),
        "stderr": completed.stderr.strip(),
    }


def block_for_result(entry):
    lines = [f"$ {entry['cmd']}"]
    if entry["stdout"]:
        lines.append(entry["stdout"])
    if entry["stderr"]:
        lines.append(entry["stderr"])
    lines.append(f"EXIT={entry['exit']}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Automate A8 kdrv command-level validation.")
    parser.add_argument("--exe", default="build/jdrive64.exe", help="Path to jdrive64 executable")
    parser.add_argument("--image", required=True, help="Path to test .d64 image")
    parser.add_argument("--drive", default="R:", help="Drive letter to use (default: R:)")
    parser.add_argument("--output", default="", help="Optional markdown evidence output path")
    parser.add_argument("--operator", default=os.getenv("USERNAME", "unknown"), help="Operator name")
    args = parser.parse_args()

    exe = Path(args.exe).resolve()
    image = Path(args.image).resolve()
    drive = args.drive.upper()

    if not exe.exists():
        print(f"ERROR: executable not found: {exe}", file=sys.stderr)
        return 2
    if not image.exists():
        print(f"ERROR: image not found: {image}", file=sys.stderr)
        return 2
    if len(drive) != 2 or not drive.endswith(":"):
        print(f"ERROR: invalid drive letter: {drive}", file=sys.stderr)
        return 2

    run_env = os.environ.copy()
    run_env["PATH"] = (
        r"C:\msys64\ucrt64\bin;C:\msys64\usr\bin;" + run_env.get("PATH", "")
    )

    git_rev = run_command(["git", "rev-parse", "--short", "HEAD"], env=run_env)
    commit_short = git_rev["stdout"] if git_rev["exit"] == 0 and git_rev["stdout"] else "unknown"

    commands = [
        [str(exe), "mount", str(image), drive],
        [str(exe), "backend-diag-mounted", drive],
        [str(exe), "mounts"],
        [str(exe), "unmount", drive],
        [str(exe), "mount", str(image), drive],
        [str(exe), "unmount", drive],
        [str(exe), "mounts"],
    ]

    results = []
    try:
        for cmd in commands:
            results.append(run_command(cmd, env=run_env))
    finally:
        # Best effort cleanup to avoid stale mount state.
        cleanup = run_command([str(exe), "unmount", drive], env=run_env)
        if cleanup["exit"] == 0:
            results.append(cleanup)

    all_zero = all(r["exit"] == 0 for r in results[:7])
    diag_ok = "Backend: kdrv" in results[1]["stdout"]
    first_mounts_ok = f"{drive} ->" in results[2]["stdout"]
    final_mounts_ok = f"{drive} ->" not in results[6]["stdout"]

    verdict = all_zero and diag_ok and first_mounts_ok and final_mounts_ok
    now = dt.datetime.now()
    date_str = now.strftime("%Y-%m-%d")
    ts = now.strftime("%Y-%m-%d %H:%M:%S")

    if args.output:
        output_path = Path(args.output)
    else:
        output_path = Path(f"A8_EVIDENCE_{date_str}_AUTO.md")

    transcript = "\n\n".join(block_for_result(r) for r in results[:7])
    content = f"""## A8 Validation Evidence (Automated kdrv Session)

## Environment

- Date: {date_str}
- Timestamp: {ts}
- Operator: {args.operator}
- Machine: {platform.node()}
- Windows version: {platform.platform()}
- JDrive64 executable: `{exe}`
- JDrive64 commit: `{commit_short}`
- Test image path: `{image}`
- Drive letter used: `{drive}`

## Command Transcript

```powershell
{transcript}
```

## Automated Checks

- All command exits are zero: {'PASS' if all_zero else 'FAIL'}
- Diagnostics report `Backend: kdrv`: {'PASS' if diag_ok else 'FAIL'}
- `mounts` contains `{drive}` right after mount: {'PASS' if first_mounts_ok else 'FAIL'}
- Final `mounts` does not contain `{drive}` after unmount: {'PASS' if final_mounts_ok else 'FAIL'}

## Manual Explorer Checks (still required for A8 closure)

- Mount visibility in Explorer: PENDING
- Read behavior (open/copy/seek): PENDING
- Denied write policy: PENDING
- Required screenshots captured: PENDING

## Final Verdict

- Command-level A8 automation: {'PASS' if verdict else 'FAIL'}
- A8 completed on this host: NO (manual Explorer evidence still required)
"""

    output_path.write_text(content, encoding="utf-8")
    print(f"Wrote evidence: {output_path}")
    print(f"Command-level verdict: {'PASS' if verdict else 'FAIL'}")
    return 0 if verdict else 1


if __name__ == "__main__":
    raise SystemExit(main())
