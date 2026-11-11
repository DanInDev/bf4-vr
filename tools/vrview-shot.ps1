# Captures SteamVR's mirror window (VR View / Headset View): the composited picture with every
# OpenXR layer (grab rings, aim dot, sight dot, scope, panel, notices), unlike pipe shot.
#   vrview-shot.ps1 <out.png>      capture
#   vrview-shot.ps1 -List          list visible top-level windows (to find the mirror's title)
param(
	[string]$Path,
	[switch]$List,
	[string]$TitlePattern = '^(VR View|Headset View|Headset Mirror|SteamVR Mirror)'
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class VrViewWin {
	public delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
	[DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lparam);
	[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
	[DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
	[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int max);
	[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
	[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT rect);
	[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
	[DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
	[StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
	public static List<KeyValuePair<IntPtr, string>> Windows() {
		var found = new List<KeyValuePair<IntPtr, string>>();
		EnumWindows((h, l) => {
			if (!IsWindowVisible(h)) return true;
			var sb = new StringBuilder(256);
			GetWindowText(h, sb, sb.Capacity);
			if (sb.Length > 0) found.Add(new KeyValuePair<IntPtr, string>(h, sb.ToString()));
			return true;
		}, IntPtr.Zero);
		return found;
	}
}
'@
[void][VrViewWin]::SetProcessDPIAware()

$windows = [VrViewWin]::Windows()
if ($List) {
	foreach ($w in $windows) {
		$procId = 0; [void][VrViewWin]::GetWindowThreadProcessId($w.Key, [ref]$procId)
		$name = (Get-Process -Id $procId -ErrorAction SilentlyContinue).ProcessName
		'{0,-20} {1}' -f $name, $w.Value
	}
	exit 0
}
if (-not $Path) { throw 'usage: vrview-shot.ps1 <out.png> | -List' }

$target = $windows | Where-Object { $_.Value -match $TitlePattern } | Select-Object -First 1
# SteamVR 2.x embeds a small headset view in its Status window: the fallback when no large mirror is open.
if (-not $target) { $target = $windows | Where-Object { $_.Value -match '^SteamVR Status' } | Select-Object -First 1 }
if (-not $target) { throw "no SteamVR mirror window open (open Headset View from the SteamVR menu; -List shows the titles)" }
if ([VrViewWin]::IsIconic($target.Key)) { throw "'$($target.Value)' is minimised: restore it (it may sit behind other windows)" }

$rect = New-Object VrViewWin+RECT
[void][VrViewWin]::GetClientRect($target.Key, [ref]$rect)
$width = $rect.Right - $rect.Left; $height = $rect.Bottom - $rect.Top
if ($width -le 0 -or $height -le 0) { throw "'$($target.Value)' has no client area" }

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$hdc = $graphics.GetHdc()
# PW_CLIENTONLY | PW_RENDERFULLCONTENT: also captures GPU-drawn content of a covered window.
$ok = [VrViewWin]::PrintWindow($target.Key, $hdc, 3)
$graphics.ReleaseHdc($hdc); $graphics.Dispose()
if (-not $ok) { $bitmap.Dispose(); throw 'PrintWindow failed' }

$dir = Split-Path ([IO.Path]::GetFullPath($Path)) -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory $dir | Out-Null }
$bitmap.Save([IO.Path]::GetFullPath($Path), [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
"vrview: '$($target.Value)' ${width}x${height} -> $Path"
