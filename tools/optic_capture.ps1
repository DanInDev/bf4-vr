param([string]$OutDir, [string]$Give = 'givegun U_SteyrAug 0 U_SteyrAug_ACOG', [string]$Name = 'acog', [float]$Behind = 6.0, [string]$Head = '0.064', [switch]$Flip)
# Monitor check of an optic (#158): the right eye put on its sight line by a fake hold, then three eye shots for
# tools\glass_magnification.py: <Name>_glass.png, <Name>_mark.png (pipe glassdebug 1: the glass draws and the line's point)
# and <Name>_ref.png (the same head, the gun lowered), plus <Name>_lens.png (scopeshot, for tools\scope_picture_rotation.py).
# -Head: fakehead arguments (ipd yaw height pitch roll); -Flip: flip a magnifier in (checks `scope` magnification=).
# Needs BF4 running with the mod (dev-run). Face lit, textured buildings: one straight edge or a window grid gives false matches.
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)
function Pipe([string]$c) { (tools\pipe.ps1 $c) -join "`n" }
function Hands([double]$dx, [double]$dy, [double]$dz) {
	Pipe ('fakehand 0 0 {0:0.000} {1:0.000} {2:0.000} {3:0.000} {4:0.000} {5:0.000}' -f (-0.024 + $dx), (-0.158 + $dy), (-0.60 + $dz), (0.054 + $dx), (-0.166 + $dy), (-0.30 + $dz)) | Out-Null
}
function Eye() {
	Start-Sleep -Milliseconds 350
	$t = Pipe 'twohand'
	if ($t -match 'off_line_cm=([-\d.]+)/([-\d.]+) along_cm=([-\d.]+)/([-\d.]+)') { return @([double]$Matches[2], [double]$Matches[4]) }
	return @(99.0, 0.0)
}
function Shot([string]$file) { 1..3 | ForEach-Object { Pipe "eyeshot2 $file" | Out-Null; Start-Sleep -Milliseconds 300 } }
Pipe 'pad 0 0 0 0 150 1000' | Out-Null
Start-Sleep 1.2
Pipe "fakehead $Head" | Out-Null
Pipe 'setting set free_hands 0' | Out-Null
Pipe 'setting set use_reach 0' | Out-Null
Pipe $Give | Out-Null
Start-Sleep 3
Pipe 'pad 0 0 0 0 10000 0300' | Out-Null
$dx = 0.0; $dy = 0.0; $dz = 0.0
Hands $dx $dy $dz
$e = Eye
for ($k = 0; $k -lt 3; $k++) { $dz -= ($e[1] + $Behind) / 100.0; Hands $dx $dy $dz; $e = Eye }
$best = $e[0]
foreach ($step in 0.02, 0.01, 0.005, 0.0025, 0.0012) {
	$improved = $true; $guard = 0
	while ($improved -and $guard -lt 25) {
		$improved = $false; $guard++
		foreach ($move in @(@($step, 0), @(-$step, 0), @(0, $step), @(0, -$step))) {
			Hands ($dx + $move[0]) ($dy + $move[1]) $dz
			$t = Eye
			if ($t[0] -lt $best) { $best = $t[0]; $dx += $move[0]; $dy += $move[1]; $improved = $true; break }
		}
		Hands $dx $dy $dz
	}
}
Pipe 'pad 0 0 0 0 9500 0300' | Out-Null
if ($Flip) {
	# D-pad up toggles the magnifier: flip until the selected level magnifies.
	for ($k = 0; $k -lt 3; $k++) {
		if ((Pipe 'scope') -match 'magnification=1\.[5-9]') { break }
		Pipe 'pad 0 0 0 0 200 0301' | Out-Null; Start-Sleep 1.5
	}
	Pipe 'pad 0 0 0 0 9000 0300' | Out-Null
}
$e = Eye
"{0}: eye {1:0.0} cm off the line, {2:0.0} cm along" -f $Name, $e[0], $e[1]
((Pipe 'scope') -split "`n") | Where-Object { $_ -match '^scope game_fov|scope_glass=' } | ForEach-Object { "  " + ($_ -replace '^(scope game_fov.*render_fov=[\d.]+).*$', '$1' -replace '^.*(scope_glass=\d+).*(power=[\d.]+ uv_per_tangent=[\d.]+ line_uv=\([^)]*\)).*(field=[\d.]+ virtual=\d+).*$', '$1 $2 $3') }
Start-Sleep 1
Shot (Join-Path $OutDir "$Name`_glass.png")
Pipe 'glassdebug 1' | Out-Null
Start-Sleep 0.5
Shot (Join-Path $OutDir "$Name`_mark.png")
Pipe 'glassdebug 0' | Out-Null
Pipe "scopeshot $(Join-Path $OutDir "$Name`_lens.png")" | Out-Null
# The gun far down and aside, the head unchanged.
Pipe 'fakehand 0 0 -0.3 -0.9 -0.2 0.3 -0.9 -0.2' | Out-Null
Pipe 'pad 0 0 0 0 100 0000' | Out-Null
Start-Sleep 1.5
Shot (Join-Path $OutDir "$Name`_ref.png")
Pipe 'setting set free_hands 1' | Out-Null
Pipe 'setting set use_reach 1' | Out-Null
Pipe 'fakehand off' | Out-Null
Pipe 'fakehead off' | Out-Null

