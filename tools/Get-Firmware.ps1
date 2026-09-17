# Fetches FW_ACC_00U.bin, the MediaTek MT7612U firmware for the Xbox Wireless
# Adapter, which Microsoft ships inside its driver package and which is not
# redistributed in this repository.
#
# Preferred source: the driver store of a PC that has (or had) the adapter with
# Microsoft's driver installed (version 21.50.45.656, the 2015 build for the
# first-generation PID_02E6 adapter). Fallback: Microsoft's 2017 package for
# the second-generation adapter, the file the Linux xow project uses. Note:
# on the first-generation adapter tested here the 2017 firmware hung the
# dongle mid-upload, so the driver-store copy is strongly preferred.
param([string]$OutDir = (Join-Path $PSScriptRoot '..\firmware'))

$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $OutDir | Out-Null
$target = Join-Path $OutDir 'FW_ACC_00U.bin'

$store = Get-ChildItem "$env:SystemRoot\System32\DriverStore\FileRepository" -Directory -Filter 'mt7612us.inf_*' -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem $_.FullName -Filter 'FW_ACC_00U.bin' -ErrorAction SilentlyContinue } | Select-Object -First 1

if ($store) {
    Copy-Item $store.FullName $target -Force
    Write-Host "Copied from driver store: $($store.FullName)"
} else {
    $url = 'http://download.windowsupdate.com/c/msdownload/update/driver/drvs/2017/07/1cd6a87c-623f-4407-a52d-c31be49e925c_e19f60808bdcbfbd3c3df6be3e71ffc52e43261e.cab'
    $cab = Join-Path $env:TEMP 'xbox-acc-2017.cab'
    Write-Host "Driver store has no copy; downloading Microsoft's 2017 package (second-gen firmware)..."
    Invoke-WebRequest -Uri $url -OutFile $cab
    $tmp = Join-Path $env:TEMP 'xbox-acc-2017'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    & "$env:SystemRoot\System32\expand.exe" $cab -F:FW_ACC_00U.bin $tmp | Out-Null
    Copy-Item (Join-Path $tmp 'FW_ACC_00U.bin') $target -Force
    Write-Host "Extracted from Microsoft's package (sha256 should be 48084d9f...)"
}

$sha = [System.Security.Cryptography.SHA256]::Create()
$hash = ([BitConverter]::ToString($sha.ComputeHash([IO.File]::ReadAllBytes($target))) -replace '-', '').ToLower()
Write-Host "$target  sha256=$hash  ($((Get-Item $target).Length) bytes)"
Write-Host "Copy it next to xow-win.exe (build\ or dist\app\)."
