# Marks an unattended run (docs/UNATTENDED.md). While the mark exists the hooks are strict:
# no pushes, no work on main, and a turn cannot end while the gate is red or stale.
#   tools\agent\run.ps1 start [-Note "issues 41, 40"]
#   tools\agent\run.ps1 stop
#   tools\agent\run.ps1 status

param(
	[Parameter(Position = 0)][ValidateSet('start', 'stop', 'status')] [string]$Action = 'status',
	[string]$Note
)

. (Join-Path $PSScriptRoot 'common.ps1')
$root = Get-RepoRoot
$marker = Get-RunMarkerPath $root

switch ($Action) {
	'start' {
		$guard = Get-AntiCheatProcesses
		if ($guard) { "Refused: $($guard -join ', ') is running (docs/SAFETY.md)."; exit 1 }
		New-Item -ItemType Directory -Force (Split-Path $marker) | Out-Null
		$game = Get-Process bf4, bf3 -ErrorAction SilentlyContinue | Select-Object -ExpandProperty ProcessName -Unique
		[pscustomobject]@{
			started = (Get-Date).ToString('s'); note = $Note
			branch = ([string](& git -C $root branch --show-current)).Trim()
			gameRunningAtStart = @($game)
		} | ConvertTo-Json | Set-Content $marker -Encoding UTF8
		"Unattended run started in $root"
		if ($game) { "NOTE: $($game -join ', ') was already running: the owner may be using it. Do not touch that game; take engine-free issues only." }
	}
	'stop' {
		if (Test-Path $marker) { Remove-Item $marker -Force; 'Unattended run stopped.' } else { 'No run was active.' }
	}
	'status' {
		if (Test-Path $marker) { 'run: ACTIVE'; Get-Content $marker -Raw } else { 'run: not active' }
		& (Join-Path $PSScriptRoot '..\gate.ps1') -Status
	}
}
