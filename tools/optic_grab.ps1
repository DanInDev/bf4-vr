param([string]$OutDir, [string]$Give = 'givegun U_SRS 0 U_SRS_Rifle_Scope', [string]$Name = 'srs', [float]$Behind = 5.0, [switch]$NoShots, [string]$ZoomMesh = '', [string]$Only = '')
# Research on an optic's own draws: the right eye on its sight line, then `reticle dump` (every stencil-tested draw of one
# first-eye render) and `reticle grab` of each small blended lens-state draw in turn (count, start, blend, order: its
# textures as .rgba, its constant buffers and vertices; tools\rgba_to_png.py makes pictures). -ZoomMesh 0|1 forces a model.
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
Pipe 'pad 0 0 0 0 150 1000' | Out-Null
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
$e = Eye
Pipe 'pad 0 0 0 0 9500 0300' | Out-Null
"{0}: eye {1:0.0} cm off the line, {2:0.0} cm along" -f $Name, $e[0], $e[1]
"hold: dx={0:0.000} dy={1:0.000} dz={2:0.000}" -f $dx, $dy, $dz
((Pipe 'scope') -split "`n") | Select-Object -First 3 | ForEach-Object { "  " + $_.Substring(0, [Math]::Min(400, $_.Length)) }
"  " + (Pipe 'reticle')
if ($ZoomMesh -ne '') { "  " + (Pipe "zoommesh $ZoomMesh"); Start-Sleep 0.5 }
$g = Join-Path $OutDir "$Name`_grab"
"  " + (Pipe "reticle grab $g")
"  " + (Pipe "reticle dump $(Join-Path $OutDir "$Name`_dump.txt")")
# Every small blended stencil-2 draw of the dump, grabbed one by one.
$seen = @{}
foreach ($l in Get-Content (Join-Path $OutDir "$Name`_dump.txt")) {
	if ($l -match '^\d+ idx (\d+) start (\d+) .*\| ref 2 depth en 1 write 0 .*\| blend 1 src (\d+) dst (\d+)' -and [int]$Matches[1] -le 128) {
		$key = "$($Matches[1]) $($Matches[2]) $($Matches[4])"
		$nth = if ($seen.ContainsKey($key)) { $seen[$key] } else { 0 }
		$seen[$key] = $nth + 1
		$ps = if ($l -match ' ps ([0-9A-F]+) ') { $Matches[1] } else { '' }
		if ($Only -ne '' -and $ps -notlike "*$Only") { continue }
		"  draw $key #$nth ps $ps : " + (Pipe "reticle grab $g`_$($key -replace ' ','_')_$nth $key $nth")
	}
}
Pipe 'pad 0 0 0 0 9500 0300' | Out-Null
if (-not $NoShots) {
	$file = Join-Path $OutDir "$Name`_eye2.png"
	1..3 | ForEach-Object { Pipe "eyeshot2 $file" | Out-Null; Start-Sleep -Milliseconds 300 }
}
Pipe 'pad 0 0 0 0 100 0000' | Out-Null
if ($ZoomMesh -ne '') { Pipe 'zoommesh off' | Out-Null }
Pipe 'setting set free_hands 1' | Out-Null
Pipe 'setting set use_reach 1' | Out-Null
Pipe 'fakehand off' | Out-Null
Pipe 'fakehead off' | Out-Null
