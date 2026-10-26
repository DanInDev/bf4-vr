# Builds bf4-release and zips what a player needs into build\package\bf-vr-<version>.zip.
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\package.ps1 [-Version 0.1.0] [-NoBuild]
# Only the files in $contents are packaged; anything else (game files, dumps, screenshots,
# PDBs, bfvr.ini, logs) fails the run. There is no installer: the zip is the release format.

param(
	[string]$Version,
	[switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$release = Join-Path $root 'build\bf4\Release'
$deps = Join-Path $root 'build\bf4\_deps'

# Native tools write progress to stderr, which Windows PowerShell would turn into terminating errors.
function Invoke-Native([string]$what, [scriptblock]$command) {
	$ErrorActionPreference = 'Continue'
	& $command
	if ($LASTEXITCODE) { throw "$what failed (exit code $LASTEXITCODE)" }
}

if (-not $Version) {
	$Version = Invoke-Native 'git describe' { git -C $root describe --tags --always --dirty 2>$null }
	if (-not $Version) { $Version = 'dev' }
}

if (-not $NoBuild) {
	Invoke-Native 'cmake configure' { cmake --preset bf4 } | Out-Host
	Invoke-Native 'cmake build' { cmake --build --preset bf4-release } | Out-Host
}

# Package name -> source. The allow-list is the whole package.
$contents = [ordered]@{
	'bfvr_launcher.exe' = Join-Path $release 'bfvr_launcher.exe'
	'dinput8.dll'       = Join-Path $release 'dinput8.dll'
	'bfvr.ini.example'  = Join-Path $root 'tools\bfvr.ini.example'
	'README.txt'        = Join-Path $root 'tools\package\README.txt'
	'LICENSE.txt'       = Join-Path $root 'tools\package\LICENSE.txt'
}
$notices = [ordered]@{
	'MinHook (statically linked; BSD 2-Clause)'                        = Join-Path $deps 'minhook-src\LICENSE.txt'
	'OpenXR loader (statically linked; Apache License 2.0)'             = Join-Path $deps 'openxr-src\LICENSE'
	'JsonCpp, used by the OpenXR loader (public domain / MIT)'          = Join-Path $deps 'openxr-src\LICENSES\LicenseRef-jsoncpp-public-domain.txt'
	'JsonCpp MIT licence text'                                          = Join-Path $deps 'openxr-src\LICENSES\MIT.txt'
}
foreach ($source in @($contents.Values) + @($notices.Values)) {
	if (-not (Test-Path $source)) { throw "Missing: $source (build bf4-release first, or drop -NoBuild)" }
}

# Both binaries must be x64 release builds of this tree.
function Get-PeMachine([string]$path) {
	$bytes = [IO.File]::ReadAllBytes($path)
	$pe = [BitConverter]::ToInt32($bytes, 0x3C)
	[BitConverter]::ToUInt16($bytes, $pe + 4)
}
foreach ($binary in 'bfvr_launcher.exe', 'dinput8.dll') {
	if ((Get-PeMachine $contents[$binary]) -ne 0x8664) { throw "$binary is not an x64 image" }
}

$packageDir = Join-Path $root 'build\package'
$staging = Join-Path $packageDir "bf-vr-$Version"
$zip = "$staging.zip"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Force $staging | Out-Null

foreach ($name in $contents.Keys) { Copy-Item $contents[$name] (Join-Path $staging $name) }
$text = foreach ($title in $notices.Keys) {
	"=" * 78
	$title
	"=" * 78
	Get-Content $notices[$title]
	''
}
$text | Set-Content (Join-Path $staging 'THIRD-PARTY-NOTICES.txt') -Encoding UTF8

$allowed = @($contents.Keys) + 'THIRD-PARTY-NOTICES.txt'
$staged = Get-ChildItem $staging -Recurse -File
$unexpected = $staged | Where-Object { $allowed -notcontains $_.Name -or $_.DirectoryName -ne $staging }
if ($unexpected) { throw "Unexpected files in the package: $($unexpected.FullName -join ', ')" }

if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $zip
Remove-Item $staging -Recurse -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try { $entries = $archive.Entries | ForEach-Object { $_.FullName } } finally { $archive.Dispose() }
$extra = $entries | Where-Object { $allowed -notcontains $_ }
if ($extra -or $entries.Count -ne $allowed.Count) { Remove-Item $zip -Force; throw "Zip contents differ from the allow-list: $($entries -join ', ')" }

"Package: $zip"
$entries | ForEach-Object { "  $_" }
"SHA-256: " + (Get-FileHash $zip -Algorithm SHA256).Hash
