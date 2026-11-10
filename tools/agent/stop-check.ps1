# Claude Code Stop hook (.claude\settings.json). During an unattended run a turn may not end while
# the gate is red, stale, or forbidden files are in the tree. Exit 2 sends the reason back to the agent.
# It objects once per stop (stop_hook_active), so a task that cannot go green can still be parked
# as agent-blocked instead of looping forever.

. (Join-Path $PSScriptRoot 'common.ps1')

$payload = $null
try { $payload = [Console]::In.ReadToEnd() | ConvertFrom-Json } catch { }
if ($payload -and $payload.stop_hook_active) { exit 0 }
try { $root = Get-RepoRoot $(if ($payload) { $payload.cwd }) } catch { exit 0 }
if (-not (Test-RunActive $root)) { exit 0 }

$problems = @()
$forbidden = Get-ForbiddenFiles $root
if ($forbidden) { $problems += "Files that must never be committed are in the tree: $(($forbidden | Select-Object -First 5) -join ', '). Remove them or move them to an ignored folder." }

$stampPath = Get-GateStampPath $root
if (-not (Test-Path $stampPath)) { $problems += 'The gate has not been run. Run tools\gate.ps1.' }
else {
	$stamp = Get-Content $stampPath -Raw | ConvertFrom-Json
	if ($stamp.state -ne (Get-SourceState $root)) { $problems += 'Sources changed since the last gate run. Run tools\gate.ps1.' }
	elseif ($stamp.result -ne 'pass') { $problems += "The gate is red ($($stamp.failed -join '; ')). Fix it, or revert the change, label the issue agent-blocked and write what was tried." }
}

if ($problems) {
	[Console]::Error.WriteLine("An unattended run is active (docs/UNATTENDED.md). Before ending this turn:`n- " + ($problems -join "`n- "))
	exit 2
}
exit 0
