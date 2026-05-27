param(
    [switch]$Stage
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$sourcePath = Join-Path $repoRoot "src/private_identity_overrides.h"
$archivePath = Join-Path $repoRoot "private_identity_overrides.h.7z"

if (-not (Test-Path -LiteralPath $sourcePath)) {
    Write-Host "[private-identity] No src/private_identity_overrides.h file found; skipping encrypted backup."
    exit 0
}

if (Test-Path -LiteralPath $archivePath) {
    $sourceItem = Get-Item -LiteralPath $sourcePath
    $archiveItem = Get-Item -LiteralPath $archivePath

    if ($archiveItem.LastWriteTimeUtc -ge $sourceItem.LastWriteTimeUtc) {
        Write-Host "[private-identity] Encrypted backup is up to date; skipping."
        exit 0
    }
}

$sevenZip = Get-Command 7z -ErrorAction SilentlyContinue
if (-not $sevenZip) {
    throw "7z.exe was not found on PATH. Install 7-Zip or Scoop's 7zip package before committing."
}

$password = $env:OPENELM_PRIVATE_BACKUP_PASSWORD
if ([string]::IsNullOrEmpty($password)) {
    $securePassword = Read-Host "Password for private_identity_overrides.h.7z" -AsSecureString
    $bstr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($securePassword)
    try {
        $password = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($bstr)
    }
    finally {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($bstr)
    }
}

if ([string]::IsNullOrEmpty($password)) {
    throw "Encrypted backup password cannot be empty."
}

if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

Push-Location $repoRoot
try {
    & $sevenZip.Source a -t7z "private_identity_overrides.h.7z" "src/private_identity_overrides.h" "-mhe=on" "-p$password" | Write-Host
}
finally {
    Pop-Location
}

if ($LASTEXITCODE -ne 0) {
    throw "7z failed with exit code $LASTEXITCODE."
}

if ($Stage) {
    & git -C $repoRoot add -- "private_identity_overrides.h.7z"
    if ($LASTEXITCODE -ne 0) {
        throw "git add failed for private_identity_overrides.h.7z."
    }
}

Write-Host "[private-identity] Updated encrypted backup: private_identity_overrides.h.7z"
