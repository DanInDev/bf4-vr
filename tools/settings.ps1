# Live settings from the desktop (issue #25): pick with up/down, change with left/right, s saves into
# bfvr.ini, r resets the selected value, q quits. Changes apply in the game within a frame.
#   tools\settings.ps1
#   tools\settings.ps1 -List            print once and exit

param([switch]$List)

$ErrorActionPreference = 'Stop'
$pipe = Join-Path $PSScriptRoot 'pipe.ps1'

function Send([string]$line) {
	@(& powershell -NoProfile -File $pipe setting $line) -join "`n"
}

function Rows {
	(Send 'list') -split "`n" | Where-Object { $_ -match '^\S+ = ' }
}

if ($List) { Send 'list'; exit 0 }

$selected = 0
$message = ''
while ($true) {
	$rows = @(Rows)
	if ($rows.Count -eq 0) { Write-Host 'no settings (is the game running with the DLL?)'; exit 1 }
	$selected = [math]::Max(0, [math]::Min($selected, $rows.Count - 1))
	Clear-Host
	Write-Host 'bf-vr live settings   up/down pick   left/right change   r reset   s save   q quit'
	Write-Host '(* = changed, not saved yet)'
	Write-Host ''
	for ($i = 0; $i -lt $rows.Count; ++$i) {
		if ($i -eq $selected) { Write-Host ('> ' + $rows[$i]) -ForegroundColor Yellow } else { Write-Host ('  ' + $rows[$i]) }
	}
	Write-Host ''
	Write-Host $message
	$name = ($rows[$selected] -split ' = ')[0]
	$key = [Console]::ReadKey($true)
	$message = ''
	switch ($key.Key) {
		'UpArrow' { --$selected }
		'DownArrow' { ++$selected }
		'LeftArrow' { $message = Send "step $name -1" }
		'RightArrow' { $message = Send "step $name +1" }
		'R' { $message = Send "reset $name" }
		'S' { $message = Send 'save' }
		'Q' { exit 0 }
	}
}
