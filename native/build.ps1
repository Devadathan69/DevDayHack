$ErrorActionPreference = "Stop"

$nativeRoot = $PSScriptRoot
$buildDirectory = Join-Path $nativeRoot "build"
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio Installer's vswhere.exe was not found. Install Visual Studio C++ build tools first."
}

$vsPath = (& $vswhere -latest -prerelease -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsPath) {
    throw "The Visual C++ x86/x64 build tools were not found. Install the Desktop development with C++ workload."
}

$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "VsDevCmd.bat was not found under the selected Visual Studio installation."
}

$sourcePath = Join-Path $nativeRoot "src\bootstrap.cpp"
$overlaySourcePath = Join-Path $nativeRoot "src\d3d-overlay.cpp"
$holdSourcePath = Join-Path $nativeRoot "src\ped-hold.cpp"
$microphoneSourcePath = Join-Path $nativeRoot "src\microphone.cpp"
$outputPath = Join-Path $buildDirectory "MalayalamNpcBootstrap.asi"
$importLibraryPath = Join-Path $buildDirectory "MalayalamNpcBootstrap.lib"
$commandFile = Join-Path $buildDirectory "build-native.cmd"

$command = @"
@echo off
call "$vsDevCmd" -no_logo -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "$buildDirectory"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /LD /MT /EHsc /std:c++17 /W4 /DUNICODE /D_UNICODE /Fe:"$outputPath" "$sourcePath" "$overlaySourcePath" "$holdSourcePath" "$microphoneSourcePath" /link /MACHINE:X86 /IMPLIB:"$importLibraryPath" d3d9.lib user32.lib
exit /b %errorlevel%
"@

Set-Content -LiteralPath $commandFile -Value $command -Encoding ASCII
try {
    & $env:ComSpec /d /c "`"$commandFile`""
    if ($LASTEXITCODE -ne 0) {
        throw "The native x86 build failed with exit code $LASTEXITCODE."
    }
}
finally {
    Remove-Item -LiteralPath $commandFile -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path -LiteralPath $outputPath)) {
    throw "The build completed without producing the expected ASI file."
}

$output = Get-Item -LiteralPath $outputPath
Write-Output "Built $($output.FullName) ($($output.Length) bytes)."
