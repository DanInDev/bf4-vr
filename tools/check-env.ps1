# Reports the bf-vr development environment. Read-only: installs and changes nothing.
# Usage: powershell -ExecutionPolicy Bypass -File tools\check-env.ps1

$ErrorActionPreference = 'SilentlyContinue'
$results = New-Object System.Collections.Generic.List[object]

function Add-Result([string]$area, [string]$item, [string]$state, [string]$detail) {
	$results.Add([pscustomobject]@{ Area = $area; Item = $item; State = $state; Detail = $detail })
}

function Find-First([string[]]$patterns) {
	foreach ($pattern in $patterns) {
		$match = Get-Item $pattern -ErrorAction SilentlyContinue | Select-Object -First 1
		if ($match) { return $match.FullName }
	}
	return $null
}

function Get-PeMachine([string]$path) {
	$stream = [IO.File]::OpenRead($path)
	try {
		$reader = New-Object IO.BinaryReader($stream)
		$stream.Position = 0x3C; $peOffset = $reader.ReadInt32()
		$stream.Position = $peOffset + 4
		switch ($reader.ReadUInt16()) { 0x14C { 'x86' } 0x8664 { 'x64' } default { 'unknown' } }
	} finally { $stream.Dispose() }
}

# --- Toolchain -------------------------------------------------------------
foreach ($tool in 'git', 'gh', 'python', 'node') {
	$command = Get-Command $tool
	if ($command) { Add-Result 'Toolchain' $tool 'OK' $command.Source } else { Add-Result 'Toolchain' $tool 'MISSING' '' }
}

$nodeVersion = (node --version) -replace '^v', ''
if ($nodeVersion) {
	$ok = [version]$nodeVersion -ge [version]'24.11.0'
	Add-Result 'Toolchain' 'node >= 24.11 (REA)' ($(if ($ok) { 'OK' } else { 'UPDATE' })) $nodeVersion
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath }
if ($vsPath) {
	Add-Result 'Toolchain' 'MSVC x86/x64' 'OK' $vsPath
	$cmake = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
	if (Get-Command cmake) { Add-Result 'Toolchain' 'cmake' 'OK' (Get-Command cmake).Source }
	elseif (Test-Path $cmake) { Add-Result 'Toolchain' 'cmake' 'OK' "bundled: use 'Developer PowerShell for VS 2022' or $cmake" }
	else { Add-Result 'Toolchain' 'cmake' 'MISSING' 'add "C++ CMake tools" in the VS Installer' }
} else {
	Add-Result 'Toolchain' 'MSVC x86/x64' 'MISSING' 'VS 2022 Build Tools with "Desktop development with C++"'
}

# --- Reverse engineering ---------------------------------------------------
# Values set with "setx"/user scope are read from the registry so a fresh shell is not required.
function Get-UserOrProcessEnv([string]$name) {
	$value = [Environment]::GetEnvironmentVariable($name, 'Process')
	if (-not $value) { $value = [Environment]::GetEnvironmentVariable($name, 'User') }
	if (-not $value) { $value = [Environment]::GetEnvironmentVariable($name, 'Machine') }
	return $value
}

# REA and Ghidra 12.1.x need a full 64-bit JDK 21+ through JAVA_HOME.
$javaHome = Get-UserOrProcessEnv 'JAVA_HOME'
$javaExe = if ($javaHome) { Join-Path $javaHome 'bin\java.exe' }
# java prints its version to stderr; run it through cmd so PowerShell 5.1 does not drop it.
$javaLine = if ($javaExe -and (Test-Path $javaExe)) { [string](cmd /c "`"$javaExe`" -version 2>&1" | Select-Object -First 1) } else { '' }
$javaMajor = if ($javaLine -match '"(\d+)') { [int]$Matches[1] } else { 0 }
Add-Result 'RE' 'JAVA_HOME -> JDK 21+' ($(if ($javaMajor -ge 21) { 'OK' } elseif ($javaMajor) { 'UPDATE' } else { 'MISSING' })) "$javaHome $javaLine"

# REA 6.7 is validated against Ghidra 12.1.x.
$ghidra = Get-UserOrProcessEnv 'GHIDRA_INSTALL_DIR'
if (-not ($ghidra -and (Test-Path $ghidra))) {
	$ghidra = Find-First @('C:\Tools\ghidra_12.1*', 'C:\Tools\ghidra*', "$env:USERPROFILE\Tools\ghidra*")
}
$ghidraState = if (-not $ghidra) { 'MISSING' } elseif ($ghidra -match 'ghidra_12\.1') { 'OK' } else { 'CHECK' }
Add-Result 'RE' 'Ghidra 12.1.x (GHIDRA_INSTALL_DIR)' $ghidraState $ghidra

# A copy installed from inside the Claude desktop app lives in its private MSIX storage and is
# invisible to other processes, so only a launcher on the real PATH counts.
$rea = Get-Command rea
$reaInPackage = $rea -and $rea.Source -match '\\Packages\\Claude_'
Add-Result 'RE' 'REA (rea-agents)' ($(if ($rea -and -not $reaInPackage) { 'OK' } else { 'MISSING' })) ($(if ($rea) { $rea.Source } else { 'install from a normal terminal: npm install -g rea-agents' }))

$x32dbgCommand = Get-Command x32dbg
$x32dbg = if ($x32dbgCommand) { $x32dbgCommand.Source } else {
	Find-First @('C:\Tools\x64dbg*\release\x32\x32dbg.exe', "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\x64dbg*\release\x32\x32dbg.exe") }
Add-Result 'RE' 'x32dbg (x64dbg)' ($(if ($x32dbg) { 'OK' } else { 'MISSING' })) $x32dbg

$renderdoc = Find-First @('C:\Program Files\RenderDoc\qrenderdoc.exe')
Add-Result 'RE' 'RenderDoc' ($(if ($renderdoc) { 'OK' } else { 'MISSING' })) $renderdoc

$frosty = Find-First @('C:\Tools\FrostyEditor\FrostyEditor.exe')
$frostyBf4 = $frosty -and (Test-Path 'C:\Tools\FrostyEditor\Profiles\BF4SDK.dll')
Add-Result 'RE' 'FrostyEditor + BF4 profile' ($(if ($frostyBf4) { 'OK' } else { 'MISSING' })) $frosty

# --- VR runtime --------------------------------------------------------------
$xr64 = (Get-ItemProperty 'HKLM:\SOFTWARE\Khronos\OpenXR\1').ActiveRuntime
$xr32 = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Khronos\OpenXR\1').ActiveRuntime
Add-Result 'VR' 'OpenXR active runtime (64-bit)' ($(if ($xr64) { 'OK' } else { 'MISSING' })) $xr64
Add-Result 'VR' 'OpenXR active runtime (32-bit)' ($(if ($xr32 -and (Test-Path $xr32)) { 'OK' } else { 'MISSING' })) $xr32
$steamxr32 = 'C:\Program Files (x86)\Steam\steamapps\common\SteamVR\steamxr_win32.json'
Add-Result 'VR' 'SteamVR 32-bit OpenXR' ($(if (Test-Path $steamxr32) { 'OK' } else { 'MISSING' })) $steamxr32

# --- Games -----------------------------------------------------------------
foreach ($game in @(
		@{ Name = 'Battlefield 4'; Exe = 'bf4.exe'; Default = 'E:\EA\Battlefield 4' },
		@{ Name = 'Battlefield 3'; Exe = 'bf3.exe'; Default = 'E:\EA\Battlefield 3' })) {
	$dir = (Get-ItemProperty "HKLM:\SOFTWARE\WOW6432Node\EA Games\$($game.Name)").'Install Dir'
	if (-not $dir) { $dir = $game.Default }
	$exe = Join-Path $dir $game.Exe
	if (Test-Path $exe) {
		Add-Result 'Game' $game.Exe 'OK' ("{0} ({1})" -f $exe, (Get-PeMachine $exe))
		$proxy = Join-Path $dir 'dinput8.dll'
		Add-Result 'Game' "$($game.Exe) proxy dinput8.dll" ($(if (Test-Path $proxy) { 'PRESENT' } else { 'ABSENT' })) 'must be ABSENT before multiplayer'
	} else {
		Add-Result 'Game' $game.Exe 'MISSING' $exe
	}
}

# --- Safety ----------------------------------------------------------------
$bf6 = Get-Process | Where-Object { $_.ProcessName -match '^bf6|battlefield ?6' }
Add-Result 'Safety' 'Battlefield 6 not running' ($(if ($bf6) { 'STOP' } else { 'OK' })) ($(if ($bf6) { 'close BF6 before using RE tools' } else { '' }))
$eaac = Get-Service EAAntiCheatService
Add-Result 'Safety' 'EA AntiCheat service' ($(if ($eaac.Status -eq 'Running') { 'STOP' } else { 'OK' })) ($(if ($eaac) { "$($eaac.Status) / $($eaac.StartType)" } else { 'not installed' }))
$pb = Get-Service PnkBstrA, PnkBstrB
Add-Result 'Safety' 'PunkBuster removed' ($(if ($pb) { 'REMOVE' } else { 'OK' })) ($(if ($pb) { 'elevated: "E:\EA\Battlefield 3\pbsvc.exe" -u' } else { 'services absent' }))

$results | Format-Table -AutoSize -Wrap | Out-String -Width 220 | Write-Output
$missing = @($results | Where-Object { $_.State -in 'MISSING', 'UPDATE', 'STOP', 'REMOVE', 'CHECK' })
Write-Output ("{0} item(s) need attention." -f $missing.Count)
