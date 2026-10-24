# One-shot development loop: build, deploy, start BF4 and optionally resume the campaign.
#   tools\dev-run.ps1                    build bf4-debug, deploy, start, wait for the main menu
#   tools\dev-run.ps1 -Resume            ...then Campaign > Resume (your saved checkpoint) and wait for the level
#   tools\dev-run.ps1 -NoBuild -Resume
# Leave mouse and keyboard alone while it clicks through the menus.

param(
	[string]$Preset = 'bf4-debug',
	[switch]$NoBuild,
	[switch]$Resume,
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

# Menu positions measured on a 1920x1080 client area, scaled to the window's current client size
# (the back buffer can lag behind a window resize).
function Click-Relative([double]$x, [double]$y) {
	$size = Invoke-Tool 'game.ps1' @('size')
	if ($size -notmatch '(\d+)x(\d+)') { throw "unexpected size: $size" }
	$px = [int]([double]$Matches[1] * $x / 1920.0)
	$py = [int]([double]$Matches[2] * $y / 1080.0)
	Invoke-Tool 'game.ps1' @('click', '-X', $px, '-Y', $py) | Out-Null
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
Invoke-Tool 'game.ps1' @('stop') | Out-Null
Invoke-Tool 'deploy.ps1' @('-Action', 'deploy', '-Game', 'bf4', '-Config', $config) | Out-Null
Remove-Item 'E:\EA\Battlefield 4\bfvr.log' -ErrorAction SilentlyContinue
Invoke-Tool 'game.ps1' @('start')
Wait-Seconds $MenuWaitSeconds
# Collect the whole reply first: Select-Object -First 1 on a live pipeline kills the child (exit 255).
$status = @(Invoke-Tool 'pipe.ps1' @('status'))
"menu: " + $status[0]

if ($Resume) {
	Click-Relative 487 70; Wait-Seconds 3     # CAMPAIGN tab
	Click-Relative 590 350; Wait-Seconds 4    # BATTLEFIELD 4 CAMPAIGN tile
	Wait-Seconds 2
	Invoke-Tool 'game.ps1' @('keys', '-Keys', 'enter') | Out-Null   # RESUME is pre-selected; Enter is more reliable than a click
	Wait-Seconds $LoadWaitSeconds
	$shot = Join-Path $root 're\screenshots\dev-run-level.png'
	Invoke-Tool 'pipe.ps1' @('shot', $shot) | Out-Null
	$status = @(Invoke-Tool 'pipe.ps1' @('status'))
	"level: " + $status[0]
	"screenshot: $shot"
}
exit 0
