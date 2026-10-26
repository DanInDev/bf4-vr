# Unattended soak run (docs/tasks/testing.md layer 5): samples the control pipe's status once a
# second while BF4 sits in a campaign level, and fails on a stall, low fps or rejected eye pairs.
#   tools\soak.ps1                       10 minutes against a game that is already in a level
#   tools\soak.ps1 -Minutes 2 -MinFps 80
#   tools\soak.ps1 -Command 'worldpass 2 0.064 0'   send a pipe command first (research builds)
# Writes re\dumps\soak-<time>.csv (ignored by git) and, on a stall, the 'threads' stack sample next
# to it. Proves the game keeps running and what the counters say; not what the headset shows.

param(
	[double]$Minutes = 10,
	[double]$MinFps = 60,
	[int]$StallSeconds = 5,       # presents frozen this long = stall
	[int]$WarmupSeconds = 5,      # fps below the floor is ignored this long after start
	[string]$Command,
	[int]$TimeoutMs = 3000
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dir = Join-Path $root 're\dumps'
New-Item -ItemType Directory -Force $dir | Out-Null
$stamp = '{0:yyyyMMdd-HHmmss}' -f (Get-Date)
$csv = Join-Path $dir "soak-$stamp.csv"

function Send-Pipe([string]$line) {
	$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'bfvr', [System.IO.Pipes.PipeDirection]::InOut)
	try {
		$pipe.Connect($TimeoutMs)
		$bytes = [Text.Encoding]::UTF8.GetBytes($line)
		$pipe.Write($bytes, 0, $bytes.Length)
		$pipe.Flush()
		# A hung game thread can leave the pipe connected but silent: bound the read too.
		$reader = New-Object System.IO.StreamReader($pipe, [Text.Encoding]::UTF8)
		$task = $reader.ReadToEndAsync()
		if (-not $task.Wait($TimeoutMs)) { return $null }
		return $task.Result
	} catch {
		return $null
	} finally {
		$pipe.Dispose()
	}
}

function Get-Number([string]$text, [string]$name) {
	if ($text -match "(?:^|\s)$name=(-?[\d.]+)") { return [double]$Matches[1] }
	return $null
}

if ($Command) {
	$reply = Send-Pipe $Command
	if ($null -eq $reply) { throw "pipe did not answer '$Command'" }
	"command: $reply"
}

'time_s,fps,presents,submitted,copies,eye_frames,eye_pairs,eye_rejected,eye_abandoned,eye_ms' | Set-Content -Encoding utf8 $csv
$start = Get-Date
$end = $start.AddMinutes($Minutes)
$lastPresents = -1
$lastChange = $start
$failure = $null
$samples = 0
$minSeen = [double]::MaxValue
$lastText = ''

while ((Get-Date) -lt $end) {
	$now = Get-Date
	$elapsed = ($now - $start).TotalSeconds
	$text = Send-Pipe 'status'
	if ($null -eq $text) {
		if (($now - $lastChange).TotalSeconds -ge $StallSeconds) { $failure = 'pipe not answering (game thread or process gone)'; break }
	} else {
		$lastText = $text
		$fps = Get-Number $text 'fps'
		$presents = Get-Number $text 'presents'
		$values = @(
			[math]::Round($elapsed, 1), $fps, $presents, (Get-Number $text 'submitted'), (Get-Number $text 'copies'),
			(Get-Number $text 'eye_frames'), (Get-Number $text 'eye_pairs'), (Get-Number $text 'eye_rejected'),
			(Get-Number $text 'eye_abandoned'), (Get-Number $text 'eye_ms'))
		($values -join ',') | Add-Content -Encoding utf8 $csv
		$samples++
		if ($null -ne $presents -and $presents -ne $lastPresents) { $lastPresents = $presents; $lastChange = $now }
		if (($now - $lastChange).TotalSeconds -ge $StallSeconds) { $failure = "presents frozen for $StallSeconds s (render or game thread stalled)"; break }
		if ($null -ne $fps -and $elapsed -ge $WarmupSeconds) {
			$minSeen = [math]::Min($minSeen, $fps)
			if ($fps -lt $MinFps) { $failure = "fps $fps below $MinFps"; break }
		}
		$rejected = Get-Number $text 'eye_rejected'
		if ($rejected -gt 0) { $failure = "eye pairs rejected: $rejected"; break }
	}
	$wait = 1000 - ((Get-Date) - $now).TotalMilliseconds
	if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
}

"samples: $samples  csv: $csv"
if ($minSeen -lt [double]::MaxValue) { "lowest fps after warm-up: $minSeen" }
if ($failure) {
	"FAIL: $failure"
	$threads = Send-Pipe 'threads'
	if ($threads) {
		$path = Join-Path $dir "soak-$stamp-threads.txt"
		$threads | Set-Content -Encoding utf8 $path
		"threads: $path"
	} else {
		'threads: no answer (the pipe thread itself is blocked or the game is gone)'
	}
	"last status:`n$lastText"
	exit 1
}
'PASS'
exit 0
