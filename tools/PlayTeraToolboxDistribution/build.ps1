[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ToolboxSource,
    [Parameter(Mandatory)][string]$ElectronDirectory,
    [Parameter(Mandatory)][string]$NsisDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$taskSource = (Resolve-Path -LiteralPath $ToolboxSource).Path
$taskRuntime = (Resolve-Path -LiteralPath $ElectronDirectory).Path
$taskCompiler = Join-Path (Resolve-Path -LiteralPath $NsisDirectory).Path 'makensis.exe'
$taskOutput = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $taskOutput) { throw 'OutputDirectory must not exist: preserve earlier builds.' }
if ((Get-FileHash -LiteralPath (Join-Path $taskRuntime 'electron.exe') -Algorithm SHA256).Hash.ToLowerInvariant() -ne '543519f53189a221698264eeb6fc4a5513a751bfe9168aa691c824c831069d89') { throw 'Unverified Electron runtime.' }
if (!(Test-Path -LiteralPath $taskCompiler -PathType Leaf)) { throw 'Verified portable NSIS compiler is required.' }
New-Item -ItemType Directory -Path $taskOutput | Out-Null
$taskPayload = Join-Path $taskOutput 'payload'
New-Item -ItemType Directory -Path $taskPayload | Out-Null
$taskFiles = @(& git -c "safe.directory=$($taskSource.Replace('\','/'))" -C $taskSource ls-files)
if ($LASTEXITCODE -or $taskFiles.Count -lt 100) { throw 'Tracked Toolbox inventory is incomplete.' }
$taskFiles += @('bin/PlayTeraExitLagTransport.exe','bin/playtera-exitlag.js','bin/playtera-distribution.js','bin/playtera-update.js')
$taskUtf8 = New-Object Text.UTF8Encoding($false)
foreach ($taskRelative in ($taskFiles | Sort-Object -Unique)) {
    if ($taskRelative -eq 'manifest.json' -or $taskRelative -match '^TeraToolbox.*\.exe$' -or $taskRelative -match '^(\.git|config\.json|logs/|node_modules/electron/)') { continue }
    $taskInput = Join-Path $taskSource $taskRelative
    $taskDestination = Join-Path $taskPayload $taskRelative
    if ((Get-Item -LiteralPath $taskInput).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Source link rejected.' }
    New-Item -ItemType Directory -Path (Split-Path $taskDestination -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $taskInput -Destination $taskDestination
    # Canonical staged text; never rewrite the checkout or normalize binary assets.
    if ([IO.Path]::GetExtension($taskRelative) -in @('.js','.json','.html','.css','.md','.txt')) {
        $taskText = [IO.File]::ReadAllText($taskDestination).Replace("`r`n", "`n")
        [IO.File]::WriteAllText($taskDestination, $taskText, $taskUtf8)
    }
}
$taskRuntimeParent = Join-Path $taskPayload 'node_modules\electron'
New-Item -ItemType Directory -Path $taskRuntimeParent -Force | Out-Null
Copy-Item -LiteralPath $taskRuntime -Destination (Join-Path $taskRuntimeParent 'dist') -Recurse
# Electron supplies require('electron') as a built-in when running its GUI.
# The verified original installer contains only dist, not npm bootstrap scripts.
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'PLAYTERA-NOTICE.txt') -Destination $taskPayload
$taskVs = & 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe' -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$taskVs) { throw 'Visual Studio C++ tools missing.' }
$taskCompile = 'call "' + $taskVs + '\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul && cl.exe /nologo /std:c++17 /W4 /WX /O2 /MT /EHsc /utf-8 "' + $PSScriptRoot + '\launcher.cpp" /link bcrypt.lib user32.lib /SUBSYSTEM:WINDOWS /OUT:"' + $taskPayload + '\PlayTeraToolbox.exe" /INCREMENTAL:NO /DYNAMICBASE /NXCOMPAT'
Push-Location $taskOutput
try {
    & cmd.exe /d /c $taskCompile
    if ($LASTEXITCODE) { throw 'Toolbox launcher wrapper build failed.' }
} finally { Pop-Location }
# Self-update inventory excludes player modules/configuration and the pinned runtime.
$taskInventory = [ordered]@{}
$taskPayloadFiles = @(Microsoft.PowerShell.Management\Get-ChildItem -LiteralPath $taskPayload -Recurse -File)
foreach ($taskFile in ($taskPayloadFiles | Sort-Object FullName)) {
    $taskRelative = $taskFile.FullName.Substring($taskPayload.Length + 1).Replace('\','/')
    if ($taskRelative -match '^(mods/|node_modules/electron/)' -or $taskRelative -match '^\.' -or $taskRelative -eq 'config.json') { continue }
    $taskInventory[$taskRelative] = [ordered]@{ sha256 = (Get-FileHash -LiteralPath $taskFile.FullName -Algorithm SHA256).Hash.ToLowerInvariant(); size = $taskFile.Length }
}
$taskManifest = [ordered]@{ schemaVersion = 1; channel = 'stable'; version = '1.0.0'; sequence = 1; files = $taskInventory }
$taskFeed = Join-Path $taskOutput 'unsigned-feed\toolbox-stable'
New-Item -ItemType Directory -Path $taskFeed -Force | Out-Null
$taskManifestPath = Join-Path $taskFeed 'manifest.json'
# Generated build metadata, not a source-file edit.
[IO.File]::WriteAllText($taskManifestPath, ($taskManifest | ConvertTo-Json -Depth 8) + "`n", $taskUtf8)
Copy-Item -LiteralPath $taskManifestPath -Destination (Join-Path $taskPayload 'manifest.json')
$taskReleaseFiles = Join-Path $taskOutput 'unsigned-feed\v1.0.0'
if ($taskInventory.Count -gt 998) { throw 'GitHub asset count would exceed the release limit.' }
New-Item -ItemType Directory -Path $taskReleaseFiles -Force | Out-Null
foreach ($taskRelative in $taskInventory.Keys) {
    $taskDestination = Join-Path $taskReleaseFiles ($taskInventory[$taskRelative].sha256 + '.bin')
    Copy-Item -LiteralPath (Join-Path $taskPayload $taskRelative) -Destination $taskDestination
}
$taskInstaller = Join-Path $taskOutput 'PlayTeraToolboxSetup.exe'
& $taskCompiler /NOCONFIG /WX "/DPAYLOAD=$taskPayload" "/DOUTPUT_FILE=$taskInstaller" (Join-Path $PSScriptRoot 'PlayTeraToolbox.nsi')
if ($LASTEXITCODE) { throw 'NSIS build failed.' }
Copy-Item -LiteralPath $taskInstaller -Destination (Join-Path $taskReleaseFiles 'PlayTeraToolboxSetup.exe')
$taskResult = [ordered]@{
    version = '1.0.0'; installer = $taskInstaller
    sha256 = (Get-FileHash -LiteralPath $taskInstaller -Algorithm SHA256).Hash.ToLowerInvariant()
    size = (Get-Item -LiteralPath $taskInstaller).Length
    payloadFileCount = @(Microsoft.PowerShell.Management\Get-ChildItem -LiteralPath $taskPayload -Recurse -File).Count
    automaticUpdatesEnabled = $false; published = $false; authenticodeSigned = $false
    sourceCommit = (& git -c "safe.directory=$($taskSource.Replace('\','/'))" -C $taskSource rev-parse HEAD)
}
[IO.File]::WriteAllText((Join-Path $taskOutput 'build-result.json'), ($taskResult | ConvertTo-Json -Depth 6) + "`n", $taskUtf8)
$taskResult | ConvertTo-Json
