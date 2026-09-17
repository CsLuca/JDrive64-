param()

$ErrorActionPreference = "Stop"

$results = New-Object System.Collections.Generic.List[object]

function Add-Result {
  param(
    [string]$Check,
    [string]$Status,
    [string]$Details
  )

  $results.Add([pscustomobject]@{
      Check = $Check
      Status = $Status
      Details = $Details
    }) | Out-Null
}

function Find-CommandPath {
  param([string]$Name)
  try {
    $cmd = Get-Command $Name -ErrorAction Stop
    return $cmd.Source
  } catch {
    return $null
  }
}

function Test-ToolInPath {
  param(
    [string]$Check,
    [string]$CommandName
  )

  $path = Find-CommandPath -Name $CommandName
  if ($null -ne $path) {
    Add-Result -Check $Check -Status "PASS" -Details $path
    return $true
  }

  Add-Result -Check $Check -Status "FAIL" -Details "$CommandName not found in PATH"
  return $false
}

function Find-WinDbgPath {
  $direct = Find-CommandPath -Name "windbg.exe"
  if ($null -ne $direct) {
    return $direct
  }

  $directX = Find-CommandPath -Name "windbgx.exe"
  if ($null -ne $directX) {
    return $directX
  }

  $kitsCandidate = "C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\windbg.exe"
  if (Test-Path -LiteralPath $kitsCandidate) {
    return $kitsCandidate
  }

  try {
    $appx = Get-AppxPackage -Name "Microsoft.WinDbg" -ErrorAction Stop
    if ($null -ne $appx -and -not [string]::IsNullOrWhiteSpace($appx.InstallLocation)) {
      $appxCandidate = Join-Path $appx.InstallLocation "windbgx.exe"
      if (Test-Path -LiteralPath $appxCandidate) {
        return $appxCandidate
      }
    }
  } catch {
  }

  return $null
}

Add-Result -Check "Host OS" -Status "INFO" -Details ((Get-CimInstance Win32_OperatingSystem).Caption)

$hypervisor = (Get-CimInstance Win32_ComputerSystem).HypervisorPresent
if ($hypervisor) {
  Add-Result -Check "Hypervisor" -Status "PASS" -Details "HypervisorPresent=True"
} else {
  Add-Result -Check "Hypervisor" -Status "WARN" -Details "HypervisorPresent=False"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path -LiteralPath $vswhere) {
  $vsPath = & $vswhere -latest -products * -property installationPath
  if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($vsPath)) {
    Add-Result -Check "Visual Studio" -Status "PASS" -Details $vsPath.Trim()
  } else {
    Add-Result -Check "Visual Studio" -Status "WARN" -Details "vswhere found but no installation detected"
  }
} else {
  Add-Result -Check "Visual Studio" -Status "WARN" -Details "vswhere.exe not found"
}

$kitsRoot = $null
try {
  $kitsRoot = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" -Name "KitsRoot10" -ErrorAction Stop).KitsRoot10
} catch {
  $kitsRoot = $null
}

if (-not [string]::IsNullOrWhiteSpace($kitsRoot) -and (Test-Path -LiteralPath $kitsRoot)) {
  Add-Result -Check "Windows Kits" -Status "PASS" -Details $kitsRoot
  $kmdfPath = Join-Path $kitsRoot "Include"
  if (Test-Path -LiteralPath $kmdfPath) {
    Add-Result -Check "WDK headers" -Status "PASS" -Details $kmdfPath
  } else {
    Add-Result -Check "WDK headers" -Status "WARN" -Details "Include path not found under KitsRoot10"
  }
} else {
  Add-Result -Check "Windows Kits" -Status "FAIL" -Details "KitsRoot10 not detected"
}

$windbgPath = Find-WinDbgPath
if ($null -ne $windbgPath) {
  Add-Result -Check "WinDbg" -Status "PASS" -Details $windbgPath
} else {
  Add-Result -Check "WinDbg" -Status "FAIL" -Details "WinDbg not found (windbg.exe/windbgx.exe)"
}
Test-ToolInPath -Check "Driver Verifier" -CommandName "verifier.exe" | Out-Null

try {
  $verifierOutput = verifier /query
  if ($LASTEXITCODE -eq 0) {
    Add-Result -Check "Verifier query" -Status "PASS" -Details "verifier /query executed"
  } else {
    Add-Result -Check "Verifier query" -Status "WARN" -Details "verifier /query returned exit code $LASTEXITCODE"
  }
} catch {
  Add-Result -Check "Verifier query" -Status "WARN" -Details "Cannot execute verifier /query"
}

$bcd = $null
try {
  $bcd = bcdedit /enum
} catch {
  $bcd = $null
}

if ($LASTEXITCODE -eq 0 -and $null -ne $bcd) {
  $joined = ($bcd -join "`n").ToLowerInvariant()
  if ($joined.Contains("testsigning") -and $joined.Contains("yes")) {
    Add-Result -Check "Test signing" -Status "PASS" -Details "testsigning appears enabled"
  } else {
    Add-Result -Check "Test signing" -Status "WARN" -Details "testsigning not enabled (or not detectable)"
  }
} else {
  $bcdCurrent = $null
  try {
    $bcdCurrent = bcdedit /enum {current}
  } catch {
    $bcdCurrent = $null
  }

  if ($LASTEXITCODE -eq 0 -and $null -ne $bcdCurrent) {
    $joinedCurrent = ($bcdCurrent -join "`n").ToLowerInvariant()
    if ($joinedCurrent.Contains("testsigning") -and $joinedCurrent.Contains("yes")) {
      Add-Result -Check "Test signing" -Status "PASS" -Details "testsigning appears enabled ({current})"
    } else {
      Add-Result -Check "Test signing" -Status "WARN" -Details "testsigning not enabled (or not detectable)"
    }
  } else {
    Add-Result -Check "Test signing" -Status "WARN" -Details "Cannot execute bcdedit (admin rights may be required)"
  }
}

"K1 Lab Self-Check Results"
$results | Format-Table -AutoSize

$failCount = ($results | Where-Object { $_.Status -eq "FAIL" }).Count
if ($failCount -gt 0) {
  exit 1
}

exit 0
