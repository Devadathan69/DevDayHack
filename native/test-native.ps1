param([string]$GameExecutable = "", [string]$AudioFile = "")
$ErrorActionPreference = "Stop"
$buildDirectory = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) { throw "Visual Studio C++ build tools are required." }
$vsPath = (& $vswhere -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if (-not $vsPath) { throw "The Visual C++ x86/x64 build tools were not found." }
$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
$source = Join-Path $PSScriptRoot "test\interaction.test.cpp"
$output = Join-Path $buildDirectory "interaction-tests.exe"
$commandFile = Join-Path $buildDirectory "test-native.cmd"
$audioOutput = Join-Path $buildDirectory "audio-thread-tests.exe"
$audioBuild = ""
if ($AudioFile) {
    $audioSource = Join-Path $PSScriptRoot "test\audio-thread.test.cpp"
    $rendererSource = Join-Path $PSScriptRoot "src\d3d-overlay.cpp"
    $audioBuild = @"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /MT /EHsc /std:c++17 /W4 /DUNICODE /D_UNICODE /Fe:"$audioOutput" "$audioSource" "$rendererSource" /link /MACHINE:X86
"@
}
$command = @"
@echo off
call "$vsDevCmd" -no_logo -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cd /d "$buildDirectory"
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /MT /EHsc /std:c++17 /W4 /Fe:"$output" "$source"
$audioBuild
exit /b %errorlevel%
"@
Set-Content -LiteralPath $commandFile -Value $command -Encoding ASCII
try {
    & $env:ComSpec /d /c "`"$commandFile`""
    if ($LASTEXITCODE -ne 0) { throw "Native test compilation failed." }
    $arguments = @()
    if ($GameExecutable) {
        if (-not (Test-Path -LiteralPath $GameExecutable -PathType Leaf)) { throw "Game executable not found." }
        $arguments += $GameExecutable
    } elseif ($AudioFile) { $arguments += "-" }
    if ($AudioFile) {
        if (-not (Test-Path -LiteralPath $AudioFile -PathType Leaf)) { throw "MP3 speech fixture not found." }
        $arguments += $AudioFile
    }
    & $output @arguments
    if ($LASTEXITCODE -ne 0) { throw "Native regression tests failed with exit code $LASTEXITCODE." }
    if ($AudioFile) {
        & $audioOutput $AudioFile
        if ($LASTEXITCODE -ne 0) { throw "Production audio-thread tests failed with exit code $LASTEXITCODE." }
    }
} finally { Remove-Item -LiteralPath $commandFile -Force -ErrorAction SilentlyContinue }
