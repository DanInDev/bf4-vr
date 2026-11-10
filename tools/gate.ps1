# The gate: what must hold before any task is called done (docs/UNATTENDED.md).
#   tools\gate.ps1            forbidden files, both presets build warning-free, all unit tests, Python tools compile
#   tools\gate.ps1 -Status    show the last result and whether it still matches the sources
# Run it from the repository to check (bf-vr, or bf3-vr as ..\bf-vr\tools\gate.ps1).
# Exit code 0 = green. The result is stamped in .claude\gate.json; the stop hook reads that stamp.

param([switch]$Status)

. (Join-Path $PSScriptRoot 'agent\common.ps1')
$root = Get-RepoRoot
$stampPath = Get-GateStampPath $root

if ($Status) {
	if (-not (Test-Path $stampPath)) { 'gate: never run'; exit 1 }
	$stamp = Get-Content $stampPath -Raw | ConvertFrom-Json
	$fresh = $stamp.state -eq (Get-SourceState $root)
	"gate: $($stamp.result) at $($stamp.time), " + $(if ($fresh) { 'matches the sources' } else { 'STALE: sources changed since' })
	if ($stamp.failed) { "failed: $($stamp.failed -join '; ')" }
	exit $(if ($fresh -and $stamp.result -eq 'pass') { 0 } else { 1 })
}

$failed = New-Object System.Collections.Generic.List[string]
function Step([string]$name, [scriptblock]$body) {
	$started = Get-Date
	$problem = & $body
	$seconds = [int]((Get-Date) - $started).TotalSeconds
	if ($problem) { "FAIL  $name (${seconds}s)"; $problem | ForEach-Object { "      $_" }; $failed.Add($name) }
	else { "PASS  $name (${seconds}s)" }
}

Step 'no dumps, game files, screenshots or profile data' {
	Get-ForbiddenFiles $root | Select-Object -First 10
}

if (Test-Path (Join-Path $root 'CMakePresets.json')) {
	Push-Location $root
	foreach ($preset in 'bf4', 'bf3') {
		Step "build $preset-debug, warning-free" {
			if (-not (Test-Path "build\$preset")) { & cmake --preset $preset 2>&1 | Out-Null }
			$output = & cmake --build --preset "$preset-debug" 2>&1 | ForEach-Object { "$_" }
			if ($LASTEXITCODE -ne 0) { return @($output | Select-String -Pattern 'error' | Select-Object -First 8 | ForEach-Object { $_.Line.Trim() }) + "exit code $LASTEXITCODE" }
			$output | Select-String -Pattern '\bwarning\s+[A-Z]+\d+' | Select-Object -First 8 | ForEach-Object { $_.Line.Trim() }
		}
		Step "unit tests $preset-debug" {
			$output = & ctest --preset "$preset-debug" 2>&1 | ForEach-Object { "$_" }
			if ($LASTEXITCODE -ne 0) { $output | Select-String -Pattern 'Failed|\*\*\*|tests failed' | Select-Object -First 8 | ForEach-Object { $_.Line.Trim() } }
		}
	}
	Pop-Location
}

if (Get-ChildItem (Join-Path $root 'tools') -Filter *.py -ErrorAction SilentlyContinue) {
	Step 'Python tools compile' {
		$output = & python -E -m compileall -q (Join-Path $root 'tools') 2>&1 | ForEach-Object { "$_" }
		if ($LASTEXITCODE -ne 0) { $output | Select-Object -First 8 }
	}
}

$result = if ($failed.Count) { 'fail' } else { 'pass' }
New-Item -ItemType Directory -Force (Split-Path $stampPath) | Out-Null
[pscustomobject]@{ result = $result; failed = @($failed); state = (Get-SourceState $root); time = (Get-Date).ToString('s') } |
	ConvertTo-Json | Set-Content $stampPath -Encoding UTF8
"GATE: $($result.ToUpper())"
exit $failed.Count
