param(
  [string]$BuildDir = "build"
)

$ErrorActionPreference = "Stop"

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

cmake -S . -B $BuildDir
if (-not $?) {
  throw "CMake configure failed"
}

cmake --build $BuildDir
if (-not $?) {
  throw "Build failed"
}

ctest --test-dir $BuildDir --output-on-failure
if (-not $?) {
  throw "CTest failed"
}

Write-Host "Release readiness checks passed for VERSION $version"
