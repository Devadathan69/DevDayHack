param([Parameter(Mandatory = $true)][string]$GameDirectory)
$ErrorActionPreference = "Stop"
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$hasExecutable = (Test-Path -LiteralPath (Join-Path $game "gta-sa.exe")) -or (Test-Path -LiteralPath (Join-Path $game "gta_sa.exe"))
if (-not $hasExecutable) { throw "The selected folder has no GTA San Andreas executable." }
if (-not (Test-Path -LiteralPath (Join-Path $game "dinput8.dll"))) {
    throw "Install an official compatible x86 ASI loader first. This script does not install or replace loader DLLs."
}
$source = Join-Path $PSScriptRoot "build\MalayalamNpcBootstrap.asi"
if (-not (Test-Path -LiteralPath $source)) { throw "Run npm run build:native before installing." }
$destination = Join-Path $game "MalayalamNpcBootstrap.asi"
try { Copy-Item -LiteralPath $source -Destination $destination -Force }
catch { throw "The project ASI could not be copied. Close GTA, check folder access, and retry. $($_.Exception.Message)" }
$builtHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$installedHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
if ($builtHash -ne $installedHash) { throw "Installed ASI verification failed." }
Write-Output "Installed only MalayalamNpcBootstrap.asi; SHA-256 matches the build. Restart GTA to load it."
