# Claude Code PreToolUse hook for shell commands (.claude\settings.json). Exit 2 blocks the command
# and shows the reason to the agent. Rules: docs/UNATTENDED.md, docs/SAFETY.md.

. (Join-Path $PSScriptRoot 'common.ps1')

$command = ''
$hint = $null
try {
	$payload = [Console]::In.ReadToEnd() | ConvertFrom-Json
	$command = [string]$payload.tool_input.command
	$hint = $payload.cwd
} catch { exit 0 }
if (-not $command) { exit 0 }

function Deny([string]$reason) { [Console]::Error.WriteLine("Blocked by tools\agent\pre-command.ps1: $reason"); exit 2 }

# 1. Always: no game or reverse-engineering tool beside Battlefield 6 / EasyAntiCheat.
$gameTool = '(?i)(pipe|dev-run|game|deploy|soak|console-probe|poison_sweep)\.ps1|tools[\\/][a-z_0-9]+_(probe|scan|sweep|watch|search)\.py|find_typed\.py|pipe\\bfvr'
if ($command -match $gameTool) {
	$guard = Get-AntiCheatProcesses
	if ($guard) { Deny "$($guard -join ', ') is running. No game or RE tool may run beside it (docs/SAFETY.md). Do engine-free work, or stop." }
}

# 2. Always: the commit check is never skipped.
if ($command -match '(?i)\bgit\b.*--no-verify') { Deny 'commit hooks are never skipped. Fix what the hook reports.' }

try { $root = Get-RepoRoot $hint } catch { exit 0 }
$branch = ([string](& git -C $root branch --show-current 2>$null)).Trim()

# 3. Always: main holds releases only (CLAUDE.md "Branches"); it moves by a fast-forward to dev and nothing else.
if ($branch -in 'main', 'master') {
	if ($command -match '(?i)\bgit\b[^|;&]*\b(commit|cherry-pick|rebase|revert)\b') {
		Deny "$branch holds releases only. Work on a branch from dev and merge it into dev."
	}
	if ($command -match '(?i)\bgit\b[^|;&]*\bmerge\b(?!-)' -and $command -notmatch '(?i)\bmerge\s+--ff-only\s+(origin/)?dev\s*($|[|;&])') {
		Deny "$branch changes only at a release: git merge --ff-only dev (CLAUDE.md ""Branches"")."
	}
}

# 4. Only during an unattended run.
if (-not (Test-RunActive $root)) { exit 0 }

if ($command -match '(?i)\bgit\b[^|;&]*\bpush\b') { Deny 'unattended runs never push. The owner pushes after review.' }
if ($command -match '(?i)\bgh\s+pr\s+merge\b|\bgh\s+issue\s+(close|delete)\b|\bgh\s+(repo|release)\b') { Deny 'unattended runs never merge pull requests, close issues or change the repository. Label the issue ready-for-check instead.' }
if ($command -match '(?i)\bgit\b[^|;&]*\b(reset\s+--hard|branch\s+-D|clean\s+-[a-z]*f)') { Deny 'destructive git commands are not allowed in an unattended run. Leave the branch and label the issue agent-blocked.' }
if ($command -match '(?i)\bsetting\s+save\b') { Deny 'live settings are not saved in an unattended run; restore what you changed.' }

if ($branch -in 'main', 'master', 'dev' -and $command -match '(?i)\bgit\b[^|;&]*\b(commit|merge|rebase|cherry-pick)\b') {
	Deny "unattended runs never change $branch. Work on the issue's own branch; collect green branches on night/<date>."
}
exit 0
