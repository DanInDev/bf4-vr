# Runs BF4 console commands and captures each answer from the back buffer.
#   & tools\console-probe.ps1 -Name stereo -Commands @('RenderDevice.StereoSeparationScale', 'list')
# Screenshots: re\screenshots\<Name>-<n>.png. Takes keyboard focus while it runs.
# The console stays open across commands; its state is read from the screenshot (white "Console" tab).

param(
	[Parameter(Mandatory)] [string[]]$Commands,
	[string]$Name = 'probe',
	[int]$SettleMs = 500
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$tools = $PSScriptRoot
$shots = Join-Path (Split-Path $tools -Parent) 're\screenshots'
New-Item -ItemType Directory -Force $shots | Out-Null

function Invoke-Tool([string]$script, [string[]]$arguments) {
	& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $tools $script) @arguments
}

function Save-Shot([string]$file) {
	$result = Invoke-Tool 'pipe.ps1' @('shot', $file)
	if ($result -notlike 'ok *') { throw "screenshot failed: $result" }
}

function Test-ConsoleOpen {
	$file = Join-Path $env:TEMP 'bfvr-console-state.png'
	Save-Shot $file
	$image = [System.Drawing.Bitmap]::FromFile($file)
	try {
		# Blank right part of the white "Console" tab (x 30..180, y 30..47), right of its black text.
		$pixel = $image.GetPixel(150, 38)
		return $pixel.R -gt 200 -and $pixel.G -gt 200 -and $pixel.B -gt 200
	} finally {
		$image.Dispose()
	}
}

function Set-Console([bool]$open) {
	for ($attempt = 0; $attempt -lt 3 -and (Test-ConsoleOpen) -ne $open; $attempt++) {
		Invoke-Tool 'game.ps1' @('keys', '-Keys', 'console') | Out-Null
		Start-Sleep -Milliseconds 1000
	}
	if ((Test-ConsoleOpen) -ne $open) { throw "could not set console open=$open" }
}

Set-Console $true
$index = 0
foreach ($command in $Commands) {
	$index++
	Invoke-Tool 'game.ps1' @('type', '-Text', $command) | Out-Null
	Start-Sleep -Milliseconds $SettleMs
	$file = Join-Path $shots ("{0}-{1}.png" -f $Name, $index)
	Save-Shot $file
	"{0}: {1} -> {2}" -f $index, $command, $file
}
Set-Console $false
