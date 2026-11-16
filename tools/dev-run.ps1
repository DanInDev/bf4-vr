# One-shot development loop: build, deploy, start BF4 and optionally resume the campaign.
#   tools\dev-run.ps1                    build bf4-debug, deploy, start, wait for the main menu
#   tools\dev-run.ps1 -Resume            ...then Campaign > Resume (your saved checkpoint) and wait for the level
#   tools\dev-run.ps1 -NoBuild -Resume
#   tools\dev-run.ps1 -NoBuild -UseRunning -Resume   navigate a BF4 that is already running
# Menus are clicked through the mod's pointer (no focus needed; you can keep working).
# Refuses (exit 1) while the launcher is in Normal play mode (PROFSAVE_profile.bfvr-mode = normal; no marker or
# -UseRunning: not checked). -Resume ends with 'loaded: <level>' and exits 2 when still in the main menu.

param(
	[string]$Preset = 'bf4-debug',
	[switch]$NoBuild,
	[switch]$Resume,
	[switch]$UseRunning,  # BF4 already at the main menu (e.g. started from the EA app): no stop/deploy/start
	[int]$MenuWaitSeconds = 60,
	[int]$LoadWaitSeconds = 100
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$root = Split-Path $tools -Parent
$env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' + [Environment]::GetEnvironmentVariable('Path', 'User')

function Invoke-Tool([string]$script, [string[]]$arguments) {
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools $script) @arguments
	if ($LASTEXITCODE) { throw "$script $($arguments -join ' ') failed" }
}

function Wait-Seconds([int]$seconds) {
	$until = (Get-Date).AddSeconds($seconds)
	while ((Get-Date) -lt $until) { Start-Sleep -Milliseconds 500 }
}

# Menu positions measured on a 1920x1080 menu. The click goes through the mod's posted-message
# pointer (pipe "pointer", texture coordinates of the back buffer), so BF4 needs no focus and the
# mouse and keyboard stay yours. Taller than 16:9 the menu sits in a centred 16:9 band.
function Click-Relative([double]$x, [double]$y) {
	$w = 1920.0; $h = 1080.0
	$report = @(Invoke-Tool 'pipe.ps1' @('status')) -join ' '
	if ($report -match 'back_buffer=(\d+)x(\d+)') { $w = [double]$Matches[1]; $h = [double]$Matches[2] }
	$band = [math]::Min($h, $w * 9.0 / 16.0)
	$u = ($w * $x / 1920.0) / $w
	$v = (($h - $band) / 2.0 + $band * $y / 1080.0) / $h
	$at = ('{0:0.0000} {1:0.0000}' -f $u, $v)
	Invoke-Tool 'pipe.ps1' @('pointer', "$at 0") | Out-Null
	Invoke-Tool 'pipe.ps1' @('pointer', "$at 1") | Out-Null
	Start-Sleep -Milliseconds 150
	Invoke-Tool 'pipe.ps1' @('pointer', "$at 0") | Out-Null
}

# The launcher's Normal play mode keeps a flat 1920x1080 profile live and removes the mod. Deployed anyway,
# the mod renders that under a 2160x2160 window and every menu click misses (2026-11-15 and -16).
$modeFile = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Battlefield 4\settings\PROFSAVE_profile.bfvr-mode'
if (-not $UseRunning -and (Test-Path $modeFile) -and ((Get-Content $modeFile -Raw).Trim() -eq 'normal')) {
	throw "The launcher is in Normal play mode ($modeFile says 'normal'): switch to VR on its Play page (or put PROFSAVE_profile.bfvr-vr live and write 'vr' there, BF4 closed) before a dev-run."
}

if (-not $NoBuild) {
	"build $Preset"
	Push-Location $root
	try {
		# cmd keeps cmake's stderr (reconfigure notices) out of PowerShell's error stream.
		$output = cmd /c "cmake --build --preset $Preset 2>&1"
		$failed = $LASTEXITCODE
		$output | Select-String -Pattern ' error |warning C' | ForEach-Object { $_.Line }
		if ($failed) { $output | Select-Object -Last 20; throw 'build failed' }
	} finally { Pop-Location }
}

$config = if ($Preset -like '*release') { 'Release' } else { 'Debug' }
if (-not $UseRunning) {
	Invoke-Tool 'game.ps1' @('stop') | Out-Null
	Invoke-Tool 'deploy.ps1' @('-Action', 'deploy', '-Game', 'bf4', '-Config', $config) | Out-Null
	# The previous run's log stays as bfvr.prev.log: a problem seen before a restart can still be read.
	Move-Item 'E:\EA\Battlefield 4\bfvr.log' 'E:\EA\Battlefield 4\bfvr.prev.log' -Force -ErrorAction SilentlyContinue
	Invoke-Tool 'game.ps1' @('start')
}
Wait-Seconds $MenuWaitSeconds
# Collect the whole reply first: Select-Object -First 1 on a live pipeline kills the child (exit 255).
$status = @(Invoke-Tool 'pipe.ps1' @('status'))
"menu: " + $status[0]

if ($Resume) {
	Click-Relative 487 70; Wait-Seconds 3     # CAMPAIGN tab
	Click-Relative 590 350; Wait-Seconds 4    # BATTLEFIELD 4 CAMPAIGN tile
	Wait-Seconds 2
	Invoke-Tool 'pipe.ps1' @('pad', '0 0 0 0 200 1000') | Out-Null   # pad A on the pre-selected RESUME (a click there is not always taken)
	Wait-Seconds $LoadWaitSeconds
	$shot = Join-Path $root 're\screenshots\dev-run-level.png'
	Invoke-Tool 'pipe.ps1' @('shot', $shot) | Out-Null
	$status = @(Invoke-Tool 'pipe.ps1' @('status'))
	"level: " + $status[0]
	"screenshot: $shot"
	# "level:" is printed either way; the log says whether the campaign level really loaded.
	$level = Select-String -Path 'E:\EA\Battlefield 4\bfvr.log' -Pattern 'safety: level (\S+)' | Select-Object -Last 1
	$name = if ($level) { $level.Matches[0].Groups[1].Value } else { 'none' }
	"loaded: $name"
	if ($name -notlike 'Levels/SP/*') { Write-Warning 'still in the main menu: the Resume clicks did not land (see CLAUDE.md Tool pitfalls)'; exit 2 }
}
exit 0
