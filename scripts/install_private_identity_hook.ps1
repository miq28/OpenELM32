$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$hookDir = Join-Path $repoRoot ".git/hooks"
$hookPath = Join-Path $hookDir "pre-commit"
$scriptPath = Join-Path $repoRoot "scripts/private_identity_backup.ps1"

if (-not (Test-Path -LiteralPath $scriptPath)) {
    throw "Missing backup script: $scriptPath"
}

if (-not (Test-Path -LiteralPath $hookDir)) {
    throw "Git hooks directory not found: $hookDir"
}

$hookContent = @"
#!/bin/sh
sh "scripts/private_identity_backup.sh" --stage
"@

Set-Content -LiteralPath $hookPath -Value $hookContent -Encoding ascii
Write-Host "[private-identity] Installed pre-commit hook: .git/hooks/pre-commit"
Write-Host "[private-identity] Set OPENELM_PRIVATE_BACKUP_PASSWORD to avoid an interactive password prompt."
