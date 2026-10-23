# Talks to the bf-vr control pipe inside the running game.
#   tools\pipe.ps1 status
#   tools\pipe.ps1 shot                 (saves to re\screenshots\pipe-<time>.png)
#   tools\pipe.ps1 shot C:\path\x.png
#   tools\pipe.ps1 help

param(
	[Parameter(Position = 0)] [string]$Command = 'status',
	[Parameter(Position = 1)] [string]$Argument,
	[int]$TimeoutMs = 5000
)

$ErrorActionPreference = 'Stop'
if ($Command -eq 'shot' -and -not $Argument) {
	$dir = Join-Path (Split-Path $PSScriptRoot -Parent) 're\screenshots'
	New-Item -ItemType Directory -Force $dir | Out-Null
	$Argument = Join-Path $dir ("pipe-{0:yyyyMMdd-HHmmss}.png" -f (Get-Date))
}
$line = if ($Argument) { "$Command $Argument" } else { $Command }

# Negative numbers would be parsed as parameter names; pass them with a leading space: eye ' -0.03'
$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'bfvr', [System.IO.Pipes.PipeDirection]::InOut)
try {
	$pipe.Connect($TimeoutMs)
} catch {
	throw "bf-vr control pipe not available (is the game running with the DLL deployed?)"
}
try {
	$bytes = [Text.Encoding]::UTF8.GetBytes($line)
	$pipe.Write($bytes, 0, $bytes.Length)
	$pipe.Flush()
	$reader = New-Object System.IO.StreamReader($pipe, [Text.Encoding]::UTF8)
	$reader.ReadToEnd().TrimEnd()
} finally {
	$pipe.Dispose()
}
