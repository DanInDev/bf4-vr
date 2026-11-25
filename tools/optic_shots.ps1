param([string]$OutDir, [string]$Give = 'givegun U_SteyrAug 0 U_SteyrAug_ACOG', [string]$Name = 'acog_close', [float]$Behind = 4.0)
# Monitor look at one optic: the right eye on its sight line, $Behind cm behind its eye point (fake hold); prints the scope
# and glass counters (`scope`: glass_arts, open_glass, field) and saves <Name>_eye1.png, <Name>_eye2.png, <Name>_lens.png.
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
function Counters() {
	$st = Pipe 'status'
	$sc = Pipe 'scope'
	$a = if ($st -match '(scope=[\d.]+ scope_renders=\d+ scope_hidden=\d+ scope_ms=[\d.]+)') { $Matches[1] } else { '-' }
	$b = if ($sc -match 'glass_composed=(\d+) glass_no_picture=(\d+)') { "composed=$($Matches[1]) no_picture=$($Matches[2])" } else { '-' }
	return "$a | $b"
}
Pipe 'pad 0 0 0 0 150 1000' | Out-Null # resume a pause menu
Start-Sleep 1.2
Pipe 'fakehead 0.064' | Out-Null
Pipe 'setting set free_hands 0' | Out-Null
Pipe 'setting set use_reach 0' | Out-Null
Pipe $Give | Out-Null
Start-Sleep 3
Pipe 'pad 0 0 0 0 10000 0300' | Out-Null
$dx = 0.0; $dy = 0.0; $dz = 0.0
Hands $dx $dy $dz
$e = Eye
# along_cm: the eye's distance along the line from the optic's eye point (negative: behind it). Bring it to -$Behind.
for ($k = 0; $k -lt 3; $k++) { $dz -= ($e[1] + $Behind) / 100.0; Hands $dx $dy $dz; $e = Eye }
$best = $e[0]
foreach ($step in 0.02, 0.01, 0.005, 0.0025, 0.0012, 0.0006) {
	$improved = $true; $guard = 0
	while ($improved -and $guard -lt 25) {
		$improved = $false; $guard++
		foreach ($move in @(@($step, 0), @(-$step, 0), @(0, $step), @(0, -$step), @($step, $step), @(-$step, -$step), @($step, -$step), @(-$step, $step))) {
			Hands ($dx + $move[0]) ($dy + $move[1]) $dz
			$t = Eye
			if ($t[0] -lt $best) { $best = $t[0]; $dx += $move[0]; $dy += $move[1]; $improved = $true; break }
		}
		Hands $dx $dy $dz
	}
}
$e = Eye
Pipe 'pad 0 0 0 0 9000 0300' | Out-Null
"{0}: eye {1:0.0} cm off the line, {2:0.0} cm along" -f $Name, $e[0], $e[1]
"before: " + (Counters)
Start-Sleep 2
"after 2 s: " + (Counters)
((Pipe 'scope') -split "`n") | Where-Object { $_ -match 'glass_arts|scope_glass=|open_glass' } | ForEach-Object { "  " + ($_ -replace '^.*(scope_glass=.*field=[\d.]+).*$', '$1') }
foreach ($eye in 1, 2) {
	$cmd = if ($eye -eq 1) { 'eyeshot' } else { 'eyeshot2' }
	$file = Join-Path $OutDir ("{0}_eye{1}.png" -f $Name, $eye)
	1..3 | ForEach-Object { Pipe "$cmd $file" | Out-Null; Start-Sleep -Milliseconds 300 }
}
Pipe "scopeshot $(Join-Path $OutDir ($Name + '_lens.png'))" | Out-Null
Pipe 'pad 0 0 0 0 100 0000' | Out-Null
Pipe 'setting set free_hands 1' | Out-Null
Pipe 'setting set use_reach 1' | Out-Null
Pipe 'fakehand off' | Out-Null
Pipe 'fakehead off' | Out-Null

