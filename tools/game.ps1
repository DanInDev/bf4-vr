# Drives Battlefield 4 for automated test runs.
#   tools\game.ps1 start [-GameArgs "..."]  launch BF4 through the EA app (like Play) and wait for its window
#   tools\game.ps1 stop                     ask BF4 to close; force after 20 s
#   tools\game.ps1 status                   process, window and log state
#   tools\game.ps1 shot  [-Out file.png]    capture the game window (works when covered)
#   tools\game.ps1 keys  -Keys "Esc,Down,Enter"   send keys as hardware scancodes (focuses BF4)
#   tools\game.ps1 click -X 640 -Y 360      left-click at window-relative pixels (focuses BF4)
#   tools\game.ps1 console -Text "PerfOverlay.DrawFps 1"   open the console (½ key), type, Enter, close
# Input actions take keyboard focus from whatever you are doing; screenshots do not.
# For screenshots of the game's own back buffer (no focus needed) use tools\pipe.ps1 shot.

param(
	[Parameter(Position = 0)][ValidateSet('start', 'stop', 'status', 'shot', 'keys', 'click', 'console', 'type', 'size')] [string]$Action = 'status',
	[string]$Out,
	[string]$Keys,
	[string]$Text,
	[switch]$KeepOpen,
	[string]$GameArgs,
	[int]$X,
	[int]$Y,
	[int]$DelayMs = 120
)

$ErrorActionPreference = 'Stop'
$installDir = ((Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\EA Games\Battlefield 4' -ErrorAction SilentlyContinue).'Install Dir')
if (-not $installDir) { $installDir = 'E:\EA\Battlefield 4' }
$installDir = $installDir.TrimEnd('\')
$shotDir = Join-Path (Split-Path $PSScriptRoot -Parent) 're\screenshots'

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public static class BfvrWin32
{
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	[StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

	[StructLayout(LayoutKind.Sequential)] struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
	[StructLayout(LayoutKind.Sequential)] struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
	[StructLayout(LayoutKind.Explicit)] struct INPUTUNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
	[StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public INPUTUNION u; }

	[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT point);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
	[DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int cmd);
	[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
	[DllImport("user32.dll")] static extern uint SendInput(uint count, INPUT[] inputs, int size);
	[DllImport("user32.dll")] static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);

	const uint KEYEVENTF_EXTENDEDKEY = 0x1, KEYEVENTF_KEYUP = 0x2, KEYEVENTF_SCANCODE = 0x8;
	const uint MOUSEEVENTF_LEFTDOWN = 0x2, MOUSEEVENTF_LEFTUP = 0x4;

	[DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();

	// Windows only lets the foreground process hand over focus; a synthetic Alt press satisfies that rule.
	// Skipped when the game already has focus, so the game never sees a stray Alt.
	// Returns false if the game still is not in front: input must then not be sent, or it would
	// land in whatever window the user is working in.
	public static bool Focus(IntPtr hwnd)
	{
		if (GetForegroundWindow() == hwnd) return true;
		keybd_event(0x12, 0, 0, IntPtr.Zero);
		keybd_event(0x12, 0, KEYEVENTF_KEYUP, IntPtr.Zero);
		ShowWindow(hwnd, 9);
		SetForegroundWindow(hwnd);
		System.Threading.Thread.Sleep(300);
		return GetForegroundWindow() == hwnd;
	}

	// PW_RENDERFULLCONTENT captures DirectX content even when the window is covered.
	public static Bitmap Capture(IntPtr hwnd)
	{
		RECT rect; GetClientRect(hwnd, out rect);
		var bitmap = new Bitmap(Math.Max(1, rect.Right), Math.Max(1, rect.Bottom));
		using (var graphics = Graphics.FromImage(bitmap))
		{
			IntPtr hdc = graphics.GetHdc();
			PrintWindow(hwnd, hdc, 3);
			graphics.ReleaseHdc(hdc);
		}
		return bitmap;
	}

	public static void Key(ushort scan, bool extended, int holdMs)
	{
		uint flags = KEYEVENTF_SCANCODE | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
		var inputs = new INPUT[1];
		inputs[0].type = 1; inputs[0].u.ki.wScan = scan; inputs[0].u.ki.dwFlags = flags;
		SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
		System.Threading.Thread.Sleep(holdMs);
		inputs[0].u.ki.dwFlags = flags | KEYEVENTF_KEYUP;
		SendInput(1, inputs, Marshal.SizeOf(typeof(INPUT)));
	}

	// Text input arrives as WM_CHAR, which consoles and text fields read.
	public static void Type(string text, int delayMs)
	{
		const uint KEYEVENTF_UNICODE = 0x4;
		foreach (char c in text)
		{
			var inputs = new INPUT[2];
			inputs[0].type = 1; inputs[0].u.ki.wScan = c; inputs[0].u.ki.dwFlags = KEYEVENTF_UNICODE;
			inputs[1].type = 1; inputs[1].u.ki.wScan = c; inputs[1].u.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
			SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
			System.Threading.Thread.Sleep(delayMs);
		}
	}

	public static void Click(IntPtr hwnd, int x, int y)
	{
		var point = new POINT { X = x, Y = y };
		ClientToScreen(hwnd, ref point);
		SetCursorPos(point.X, point.Y);
		var inputs = new INPUT[2];
		inputs[0].type = 0; inputs[0].u.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
		inputs[1].type = 0; inputs[1].u.mi.dwFlags = MOUSEEVENTF_LEFTUP;
		SendInput(2, inputs, Marshal.SizeOf(typeof(INPUT)));
	}
}
'@

# Set-1 scancodes; DirectInput games read these rather than virtual-key codes. E = extended key.
$scancodes = @{
	esc = 0x01; enter = 0x1C; space = 0x39; tab = 0x0F; backspace = 0x0E; console = 0x29
	up = 'E48'; down = 'E50'; left = 'E4B'; right = 'E4D'
	w = 0x11; a = 0x1E; s = 0x1F; d = 0x20; e = 0x12; q = 0x10; r = 0x13; f = 0x21; g = 0x22
	f1 = 0x3B; f2 = 0x3C; f3 = 0x3D; f4 = 0x3E; f5 = 0x3F; f6 = 0x40; f7 = 0x41; f8 = 0x42; f9 = 0x43; f10 = 0x44
	'1' = 0x02; '2' = 0x03; '3' = 0x04; '4' = 0x05; '5' = 0x06
}

# The content IDs bf4.exe itself hands to the EA app (EALaunchHelper.log); the offer key the Play
# button uses (OFB-EAST:109548960) is rejected over the link as "EntitlementNotFound".
$eaContentIds = '1007968,1011575,1011576,1011577,1010268,1010269,1010270,1010271,1010958,1010959,1010960,1010961,1007077,1016751,1016757,1016754,1015365,1015364,1015363,1015362'
$eaLog = Join-Path $env:LOCALAPPDATA 'Electronic Arts\EA Desktop\Logs\EADesktop.log'

# True when the EA app logged an "AlreadyRunning" launch error after $since (UTC).
function Test-EaRefusedLaunch([datetime]$since) {
	if (-not (Test-Path $eaLog)) { return $false }
	foreach ($line in (Get-Content $eaLog -Tail 3000 | Select-String 'Game launch error: \[4\] \(AlreadyRunning\)')) {
		# Lines look like "  1582<TAB>[2026-10-25T00:39:01.669Z]<TAB>PID: ...".
		if ($line.Line -match '\[(\d{4}-\d\d-\d\dT[0-9:.]+)Z\]' -and [datetime]::Parse($Matches[1]) -ge $since) { return $true }
	}
	return $false
}

function Get-GameProcess { Get-Process bf4 -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1 }
function Require-Window {
	$process = Get-GameProcess
	if (-not $process) { throw 'BF4 is not running with a window.' }
	return $process.MainWindowHandle
}

switch ($Action) {
	'start' {
		if (Get-GameProcess) { 'BF4 already running'; break }
		# Launch the way the EA app's Play button does (starting bf4.exe directly is refused while the
		# EA app still believes an earlier instance is running).
		$link = "origin2://game/launch/?offerIds=$eaContentIds"
		if ($GameArgs) { $link += '&cmdParams=' + [Uri]::EscapeDataString($GameArgs) }
		# Only a refused launch is retried; a launch that is merely slow is waited for, never doubled.
		for ($attempt = 1; $attempt -le 3; $attempt++) {
			$launchedAt = (Get-Date).ToUniversalTime()
			Start-Process $link
			$deadline = (Get-Date).AddMinutes(3)
			$refused = $false
			while (-not (Get-GameProcess) -and (Get-Date) -lt $deadline -and -not $refused) {
				Start-Sleep -Seconds 2
				$refused = Test-EaRefusedLaunch $launchedAt
			}
			if (-not $refused) { break }
			"EA app refused launch attempt $attempt (AlreadyRunning); retrying in 20 s"
			Start-Sleep -Seconds 20
		}
		if (Get-GameProcess) { 'BF4 window is up' } else { throw 'BF4 did not open a window within 3 minutes (check the EA app for a prompt).' }
	}
	'stop' {
		$process = Get-Process bf4 -ErrorAction SilentlyContinue
		if (-not $process) { 'BF4 not running'; break }
		[void]$process.CloseMainWindow()
		$result = if (-not $process.WaitForExit(20000)) { Stop-Process -Id $process.Id -Force; 'BF4 force-closed after 20 s' } else { 'BF4 closed' }
		# The EA app tracks BFLauncher.exe, which exits ~10 s after the game; relaunching before it is
		# gone is refused as "AlreadyRunning".
		$deadline = (Get-Date).AddSeconds(60)
		while ((Get-Process bf4, BFLauncher -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 1 }
		Start-Sleep -Seconds 5
		$result
	}
	'status' {
		$process = Get-Process bf4 -ErrorAction SilentlyContinue
		"running: " + [bool]$process
		if ($process) { "window:  '$($process.MainWindowTitle)' handle=$($process.MainWindowHandle) memory=$([math]::Round($process.WorkingSet64 / 1MB)) MB" }
		$log = Join-Path $installDir 'bfvr.log'
		if (Test-Path $log) { "log tail:"; Get-Content $log -Tail 10 | ForEach-Object { "  $_" } }
	}
	'shot' {
		$window = Require-Window
		New-Item -ItemType Directory -Force $shotDir | Out-Null
		if (-not $Out) { $Out = Join-Path $shotDir ("bf4-{0:yyyyMMdd-HHmmss}.png" -f (Get-Date)) }
		$bitmap = [BfvrWin32]::Capture($window)
		$bitmap.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
		$bitmap.Dispose()
		$Out
	}
	'keys' {
		$window = Require-Window
		if (-not [BfvrWin32]::Focus($window)) { throw 'BF4 is not in the foreground; input not sent (it would go to another window).' }
		foreach ($name in ($Keys -split ',')) {
			$code = $scancodes[$name.Trim().ToLower()]
			if ($null -eq $code) { throw "Unknown key '$name'. Known: $($scancodes.Keys -join ', ')" }
			$extended = $code -is [string]
			$scan = if ($extended) { [Convert]::ToUInt16($code.Substring(1), 16) } else { [uint16]$code }
			[BfvrWin32]::Key($scan, $extended, 60)
			Start-Sleep -Milliseconds $DelayMs
		}
		"sent: $Keys"
	}
	'click' {
		$window = Require-Window
		if (-not [BfvrWin32]::Focus($window)) { throw 'BF4 is not in the foreground; input not sent (it would go to another window).' }
		[BfvrWin32]::Click($window, $X, $Y)
		"clicked ($X, $Y)"
	}
	'size' {
		# Client-area size in pixels: the coordinate space of 'click'.
		$window = Require-Window
		$rect = New-Object BfvrWin32+RECT
		[void][BfvrWin32]::GetClientRect($window, [ref]$rect)
		"{0}x{1}" -f $rect.Right, $rect.Bottom
	}
	'type' {
		# Types into whatever has input (e.g. an already open console) and presses Enter.
		$window = Require-Window
		if (-not [BfvrWin32]::Focus($window)) { throw 'BF4 is not in the foreground; input not sent (it would go to another window).' }
		[BfvrWin32]::Type($Text, 30); Start-Sleep -Milliseconds 250
		[BfvrWin32]::Key(0x1C, $false, 60); Start-Sleep -Milliseconds 800
		"typed: $Text"
	}
	'console' {
		# The console key is scancode 0x29 (½ on Nordic layouts, ` on US).
		$window = Require-Window
		if (-not [BfvrWin32]::Focus($window)) { throw 'BF4 is not in the foreground; input not sent (it would go to another window).' }
		# The console slides in; text typed before it is ready is lost.
		[BfvrWin32]::Key(0x29, $false, 60); Start-Sleep -Milliseconds 1000
		[BfvrWin32]::Type($Text, 30); Start-Sleep -Milliseconds 250
		[BfvrWin32]::Key(0x1C, $false, 60); Start-Sleep -Milliseconds 800
		if (-not $KeepOpen) { [BfvrWin32]::Key(0x29, $false, 60) }
		"console: $Text"
	}
}
