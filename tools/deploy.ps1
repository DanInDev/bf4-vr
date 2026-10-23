# Deploys, removes or reports the bf-vr proxy dinput8.dll in a game folder.
# Usage:
#   powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action deploy [-Game bf4] [-Config Debug]
#   powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action remove [-Game bf4]
#   powershell -ExecutionPolicy Bypass -File tools\deploy.ps1 -Action status [-Game bf4]
# Only a dinput8.dll that this script deployed (matching the recorded hash) is ever replaced or removed.

param(
	[ValidateSet('deploy', 'remove', 'status')] [string]$Action = 'status',
	[ValidateSet('bf4', 'bf3')] [string]$Game = 'bf4',
	[ValidateSet('Debug', 'Release')] [string]$Config = 'Debug'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$games = @{
	bf4 = @{ Name = 'Battlefield 4'; Exe = 'bf4.exe'; Default = 'E:\EA\Battlefield 4' }
	bf3 = @{ Name = 'Battlefield 3'; Exe = 'bf3.exe'; Default = 'E:\EA\Battlefield 3' }
}
$info = $games[$Game]
$installDir = (Get-ItemProperty "HKLM:\SOFTWARE\WOW6432Node\EA Games\$($info.Name)" -ErrorAction SilentlyContinue).'Install Dir'
if (-not $installDir) { $installDir = $info.Default }
$installDir = $installDir.TrimEnd('\')

$target = Join-Path $installDir 'dinput8.dll'
$marker = Join-Path $installDir 'bfvr.deployed.json'
$log = Join-Path $installDir 'bfvr.log'
$source = Join-Path $root "build\$Game\$Config\dinput8.dll"

function Get-Marker { if (Test-Path $marker) { Get-Content $marker -Raw | ConvertFrom-Json } }
function Get-Hash([string]$path) { (Get-FileHash $path -Algorithm SHA256).Hash }
function Test-GameRunning { [bool](Get-Process -Name ([IO.Path]::GetFileNameWithoutExtension($info.Exe)) -ErrorAction SilentlyContinue) }
function Assert-OwnedOrAbsent {
	if (-not (Test-Path $target)) { return }
	$recorded = Get-Marker
	if (-not $recorded -or $recorded.sha256 -ne (Get-Hash $target)) {
		throw "$target exists and was not deployed by bf-vr (another mod such as a proxy or ReShade?). Leaving it untouched."
	}
}

switch ($Action) {
	'deploy' {
		if (-not (Test-Path $source)) { throw "Build output missing: $source. Run: cmake --build --preset $Game-$($Config.ToLower())" }
		if (Test-GameRunning) { throw "$($info.Exe) is running; close it first so the next start loads the new DLL." }
		Assert-OwnedOrAbsent
		Copy-Item $source $target -Force
		[pscustomobject]@{ game = $Game; config = $Config; source = $source; sha256 = (Get-Hash $target); deployed = (Get-Date).ToString('s') } |
			ConvertTo-Json | Set-Content $marker -Encoding UTF8
		"Deployed $Config build to $target"
		"Start the game, then run: tools\deploy.ps1 -Action status -Game $Game"
	}
	'remove' {
		if (Test-GameRunning) { throw "$($info.Exe) is running; close it first." }
		Assert-OwnedOrAbsent
		if (Test-Path $target) { Remove-Item $target -Force; "Removed $target" } else { "No proxy deployed in $installDir" }
		if (Test-Path $marker) { Remove-Item $marker -Force }
	}
	'status' {
		$recorded = Get-Marker
		"game:      $($info.Name) ($installDir)"
		"proxy:     " + $(if (Test-Path $target) { if ($recorded -and $recorded.sha256 -eq (Get-Hash $target)) { "bf-vr $($recorded.config), deployed $($recorded.deployed)" } else { 'FOREIGN dinput8.dll (not ours)' } } else { 'absent' })
		"running:   " + (Test-GameRunning)
		if (Test-Path $log) { "log (last 15 lines of $log):"; Get-Content $log -Tail 15 | ForEach-Object { "  $_" } } else { "log:       none yet" }
	}
}
