# Research: zeroes one constant-buffer category in the second eye at a time and screenshots it
# (pair rendering must be on and a level loaded; re/notes/stereo-rendering.md). Shows which constant
# blocks draw what, e.g. which ones place the first-person gun.
#   tools\poison_sweep.ps1                 default categories into re\screenshots\poison-*.png
#   tools\poison_sweep.ps1 -Categories 'V 2 512','V 0 3072'

param(
	[string[]]$Categories = @('V 0 256', 'V 0 3072', 'V 0 5888', 'V 0 12800', 'V 1 256', 'V 2 512', 'P 0 256', 'P 1 256', 'P 2 512')
)

$ErrorActionPreference = 'Stop'
$pipe = Join-Path $PSScriptRoot 'pipe.ps1'
$dir = Join-Path (Split-Path $PSScriptRoot -Parent) 're\screenshots'
New-Item -ItemType Directory -Force $dir | Out-Null

& powershell -NoProfile -File $pipe eyepoison off | Out-Null
Start-Sleep -Milliseconds 500
& powershell -NoProfile -File $pipe shot (Join-Path $dir 'poison-none.png')
foreach ($category in $Categories) {
	& powershell -NoProfile -File $pipe eyepoison $category | Out-Null
	Start-Sleep -Milliseconds 500
	$name = 'poison-' + ($category -replace ' ', '-') + '.png'
	& powershell -NoProfile -File $pipe shot (Join-Path $dir $name)
}
& powershell -NoProfile -File $pipe eyepoison off
