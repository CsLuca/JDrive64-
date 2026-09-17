param(
  [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

$msysBash = "C:\msys64\usr\bin\bash.exe"

function Invoke-ToolCommand {
  param(
    [string]$Command,
    [string]$FailureMessage
  )

  $nativeAvailable = $null -ne (Get-Command ($Command.Split(' ')[0]) -ErrorAction SilentlyContinue)
  if ($nativeAvailable) {
    Invoke-Expression $Command
    if (-not $?) {
      throw $FailureMessage
    }
    return
  }

  if (-not (Test-Path -LiteralPath $msysBash)) {
    throw "Required tool not found and MSYS2 bash is unavailable: $($Command.Split(' ')[0])"
  }

  & $msysBash -lc "export PATH=/ucrt64/bin:/usr/bin:`$PATH; $Command"
  if (-not $?) {
    throw $FailureMessage
  }
}

function Assert-FileExists {
  param([string]$Path)
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Required file missing: $Path"
  }
}

Assert-FileExists "VERSION"
Assert-FileExists "CHANGELOG.md"
Assert-FileExists "V1_RELEASE_CHECKLIST.md"
Assert-FileExists "A8_EXPLORER_CHECKLIST.md"

$version = (Get-Content -LiteralPath "VERSION" -TotalCount 1).Trim()
if ([string]::IsNullOrWhiteSpace($version)) {
  throw "VERSION is empty"
}

$status = git status --porcelain
if ($status) {
  throw "Git worktree is not clean"
}

Invoke-ToolCommand -Command "cmake -S . -B $BuildDir" -FailureMessage "CMake configure failed"
Invoke-ToolCommand -Command "cmake --build $BuildDir" -FailureMessage "Build failed"
Invoke-ToolCommand -Command "ctest --test-dir $BuildDir --output-on-failure" -FailureMessage "CTest failed"

Write-Host "Release readiness checks passed for VERSION $version"
