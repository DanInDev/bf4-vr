param([string]$OutDir, [string]$Give = 'givegun U_Type88 0 U_Type88_PKA', [int]$Shots = 8, [string]$Modes = '11 00 10 01')
# #162: the second-eye stray (gun pieces drawn with the first eye's camera). Holds an optic at the right eye (fake head
# and hands, as optic_shots.ps1), then per mode (two digits: eyefix, eyefresh) takes $Shots second-eye shots of the same pose and scores each
# shot's lower half against the median shot (mean absolute difference, 0-255): a steady eye stays below ~0.5.
$ErrorActionPreference = 'Continue'
Set-Location (Split-Path -Parent $PSScriptRoot)
function Pipe([string]$c) { (tools\pipe.ps1 $c) -join "`n" }
$null = tools\optic_shots.ps1 -OutDir $OutDir -Give $Give -Name stray_hold
foreach ($mode in $Modes -split ' ') {
	Pipe ("eyefix " + $mode[0]) | Out-Null
	Pipe ("eyefresh " + $mode[1]) | Out-Null
	Pipe 'pad 0 0 0 0 9000 0300' | Out-Null
	Start-Sleep -Milliseconds 500
	for ($i = 0; $i -lt $Shots; $i++) {
		Pipe ("eyeshot2 " + (Join-Path $OutDir ("stray_m{0}_{1}.png" -f $mode, $i))) | Out-Null
		Start-Sleep -Milliseconds 450
	}
	Pipe 'pad 0 0 0 0 9000 0300' | Out-Null
}
Pipe 'eyefix 1' | Out-Null
Pipe 'eyefresh 1' | Out-Null
python -E -c @"
import sys, glob, os
from PIL import Image, ImageChops, ImageStat
d = sys.argv[1]
for mode in sys.argv[2].split():
    files = sorted(glob.glob(os.path.join(d, 'stray_m%s_*.png' % mode)))
    imgs = []
    for f in files:
        im = Image.open(f).convert('L'); w, h = im.size
        imgs.append(im.crop((0, h // 2, w, h)).resize((240, 120)))
    scores = []
    for a in imgs:
        scores.append(sorted(ImageStat.Stat(ImageChops.difference(a, b)).mean[0] for b in imgs)[len(imgs) // 2])
    print('eyefix/eyefresh %s: ' % mode + ' '.join('%.1f' % s for s in scores) + '  (shots over 1.0: %d of %d)' % (sum(s > 1.0 for s in scores), len(scores)))
"@ $OutDir $Modes
