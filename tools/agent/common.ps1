# Shared by tools\gate.ps1 and the Claude Code hooks (docs/UNATTENDED.md).
# Works in any repository that calls it: the root is the hook's project folder or the current folder.

$ErrorActionPreference = 'Continue'

function Get-RepoRoot([string]$hint) {
	foreach ($candidate in @($hint, $env:CLAUDE_PROJECT_DIR, (Get-Location).Path)) {
		if (-not $candidate -or -not (Test-Path $candidate)) { continue }
		$root = & git -C $candidate rev-parse --show-toplevel 2>$null
		if ($LASTEXITCODE -eq 0 -and $root) { return ([string]$root).Trim() }
	}
	throw 'Not inside a git repository.'
}

function Get-RunMarkerPath([string]$root) { Join-Path $root '.claude\run-active.json' }
function Get-GateStampPath([string]$root) { Join-Path $root '.claude\gate.json' }
function Test-RunActive([string]$root) { Test-Path (Get-RunMarkerPath $root) }

# Files that must never be in the repository (docs/SAFETY.md): dumps, game files, screenshots, profile data.
$script:ForbiddenPattern = '(?i)\.(dmp|exe|gpr|i64|idb|rdc)$|^re/(dumps|projects|screenshots|assets)/|PROFSAVE|(^|/)bfvr\.(ini|log)$|(^|/)bfvr_marks/'

function Get-ForbiddenFiles([string]$root) {
	$files = & git -C $root ls-files --cached --others --exclude-standard 2>$null
	@($files | Where-Object { $_ -match $script:ForbiddenPattern })
}

# One hash over every file that can change the build or the tests. It does not change when a commit
# is made or when only documents change, so a green gate stays valid across those.
function Get-SourceState([string]$root) {
	$paths = @('src', 'tests', 'tools', 'third_party', 'cmake', 'CMakeLists.txt', 'CMakePresets.json') |
		Where-Object { Test-Path (Join-Path $root $_) }
	if (-not $paths) { return 'no-sources' }
	$files = @(& git -C $root ls-files --cached --others --exclude-standard -- $paths 2>$null |
		Where-Object { Test-Path (Join-Path $root $_) -PathType Leaf } | Sort-Object)
	if (-not $files) { return 'no-sources' }
	$hashes = $files | & git -C $root hash-object --stdin-paths 2>$null
	$text = ($files -join "`n") + "`n" + ($hashes -join "`n")
	$sha = [Security.Cryptography.SHA1]::Create()
	-join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text)) | ForEach-Object { $_.ToString('x2') })
}

# Battlefield 6 and EasyAntiCheat games: no game or reverse-engineering tool may run beside them.
function Get-AntiCheatProcesses {
	@(Get-Process | Where-Object { $_.ProcessName -match '^bf6|battlefield ?6|EasyAntiCheat|EAAntiCheat\.GameService' } |
		Select-Object -ExpandProperty ProcessName -Unique)
}
