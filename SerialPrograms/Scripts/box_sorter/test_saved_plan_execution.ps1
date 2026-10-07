param(
    [Parameter(Mandatory=$true)][string]$QtRoot,
    [string]$BuildDirectory = (Join-Path $env:TEMP 'pokemon-home-executor-tests')
)
$ErrorActionPreference='Stop'
$qt=(Resolve-Path -LiteralPath $QtRoot).Path
$testSource=Join-Path $PSScriptRoot 'test_saved_plan_execution.cpp'
$work=$BuildDirectory
New-Item -ItemType Directory -Force -Path $work | Out-Null
$vs= & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC x64 Build Tools not found.' }
$batch=@"
@echo off
call "$vs\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "$work"
cl /nologo /EHsc /std:c++17 /Zc:__cplusplus /permissive- /MD /W4 /I"$qt\include" /I"$qt\include\QtCore" "$testSource" /Fe:executor-tests.exe /link /LIBPATH:"$qt\lib" Qt6Core.lib
if errorlevel 1 exit /b 1
set "PATH=$qt\bin;%PATH%"
executor-tests.exe
"@
$batchPath=Join-Path $work 'run-tests.cmd'
Set-Content -LiteralPath $batchPath -Value $batch -Encoding ascii
& cmd.exe /d /c $batchPath
if($LASTEXITCODE -ne 0){throw 'Native executor safety tests failed'}
