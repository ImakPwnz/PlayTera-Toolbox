[CmdletBinding()]
param([string]$OutputDirectory)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (!$OutputDirectory) { throw 'Specify an isolated OutputDirectory.' }
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'Visual Studio C++ tools are required.' }
$taskVcvars = Join-Path $taskVs 'VC\Auxiliary\Build\vcvarsall.bat'
$taskCompile = 'call "' + $taskVcvars + '" x86 >nul && cl.exe /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc /utf-8 "' + $PSScriptRoot + '\transport.cpp" /link ws2_32.lib advapi32.lib bcrypt.lib /OUT:"' + $taskOutput + '\PlayTeraExitLagTransport.exe" /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT'
Push-Location $taskOutput
try {
    & cmd.exe /d /c $taskCompile
    if ($LASTEXITCODE) { throw 'Transport build failed.' }
} finally { Pop-Location }
