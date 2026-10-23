# Names the NVAPI function IDs in a bfvr.log written with nvapi_probe = 1.
#   tools\nvapi-names.ps1 re\notes\nvapi-probe-stereo1.log
# IDs come from NVIDIA's nvapi_interface.h (re\ref\, MIT); unknown IDs are internal/undocumented.

param([Parameter(Mandatory)] [string]$Log)

$header = Join-Path (Split-Path $PSScriptRoot -Parent) 're\ref\nvapi_interface.h'
$names = @{}
foreach ($match in [regex]::Matches((Get-Content $header -Raw), '"(\w+)",\s*0x([0-9a-fA-F]+)')) {
	$names[[Convert]::ToUInt32($match.Groups[2].Value, 16)] = $match.Groups[1].Value
}

foreach ($line in Get-Content $Log) {
	if ($line -match 'nvapi: query 0x([0-9a-f]{8})\s.*-> (.+)$') {
		$id = [Convert]::ToUInt32($Matches[1], 16)
		$name = if ($names.ContainsKey($id)) { $names[$id] } else { '(undocumented)' }
		"{0,-10} {1,-50} {2}" -f ('0x' + $Matches[1]), $name, $Matches[2]
	}
}
