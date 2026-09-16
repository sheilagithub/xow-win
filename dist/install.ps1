# xow-win silent installer (runs elevated; launched by Install-Xbox-Wireless-Driver.cmd)
# Steps: trust the WinUSB driver package, bind the Xbox Wireless Adapter to WinUSB,
# install ViGEmBus, copy the driver, register it to start at login, start it.
param(
    [string]$InstallDir = "$env:LOCALAPPDATA\xow-win",
    [string]$StartupDir = "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\Startup",
    [string]$DesktopDir = [Environment]::GetFolderPath('Desktop'),
    # The logged-on user the driver should run as (passed in by the .cmd,
    # since this script itself runs under the elevating account)
    [string]$UserName = [Security.Principal.WindowsIdentity]::GetCurrent().Name,
    [switch]$NoStart
)

$ErrorActionPreference = 'Stop'
$src = $PSScriptRoot
$log = Join-Path $env:TEMP 'xow-win-install.log'
function L($m) { $line = (Get-Date -Format 'HH:mm:ss') + '  ' + $m; Write-Host $line; Add-Content -Path $log -Value $line }

L "=== xow-win install from $src ==="

$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')
if (-not $isAdmin) { L "ERROR: must run elevated. Use Install-Xbox-Wireless-Driver.cmd."; exit 1 }

# --- 0. Stop a running copy (it holds the dongle) ---------------------------
$TaskName = 'xow-win Xbox Wireless Driver'
Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
try {
    $e = [System.Threading.EventWaitHandle]::OpenExisting('Local\xow-win-stop'); [void]$e.Set()
    L "Stop signal sent to running xow-win"; Start-Sleep 3
    [void]$e.Reset(); $e.Close()   # clear it, or the next instance stops at once
} catch { }
Get-Process xow-win* -ErrorAction SilentlyContinue | ForEach-Object { L "Killing leftover $($_.ProcessName) ($($_.Id))"; Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
Start-Sleep 1
# Remove the old Startup-folder launcher (Defender dislikes that pattern)
Remove-Item (Join-Path $StartupDir 'xow-win.vbs') -Force -ErrorAction SilentlyContinue

# --- 1. Trust the (self-signed, libwdi-generated) WinUSB driver package ------
$cer = Join-Path $src 'winusb-driver\libwdi-xbox-acc.cer'
foreach ($store in 'Root', 'TrustedPublisher') {
    $r = & certutil.exe -addstore -f $store $cer 2>&1
    L ("Certificate -> {0}: {1}" -f $store, ($r | Select-String 'added|already|FAILED' | Select-Object -First 1))
}

# --- 2. Bind the adapter to WinUSB (force, like Zadig) -----------------------
$inf = (Resolve-Path (Join-Path $src 'winusb-driver\xbox_acc.inf')).Path
$r = & pnputil.exe /add-driver $inf 2>&1
L ("Driver store: " + (($r | Select-String 'Published|already|Failed|success' | Select-Object -First 1) -replace '\s+', ' '))

Add-Type -Namespace XowWin -Name NewDev -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("newdev.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern bool UpdateDriverForPlugAndPlayDevices(System.IntPtr hwnd, string hardwareId, string infPath, uint flags, out bool rebootRequired);
'@
$reboot = $false
$INSTALLFLAG_FORCE = 1
$present = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'USB\VID_045E&PID_02E6*' -or $_.InstanceId -like 'USB\VID_045E&PID_02FE*' -or $_.InstanceId -like 'USB\VID_045E&PID_091E*' }
if ($present) {
    $ok = [XowWin.NewDev]::UpdateDriverForPlugAndPlayDevices([IntPtr]::Zero, 'USB\VID_045E&PID_02E6', $inf, $INSTALLFLAG_FORCE, [ref]$reboot)
    $err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    L ("Bind adapter to WinUSB: {0} (win32 error {1}, reboot required: {2})" -f $ok, $err, $reboot)
    Start-Sleep 2
    $svc = (Get-PnpDeviceProperty -InstanceId $present[0].InstanceId -KeyName 'DEVPKEY_Device_Service' -ErrorAction SilentlyContinue).Data
    L "Adapter driver service now: $svc"
} else {
    L "Adapter not plugged in: driver package staged, it binds when the adapter is plugged in"
}

# --- 3. ViGEmBus (virtual controller bus) ------------------------------------
$vigem = Get-PnpDevice -ErrorAction SilentlyContinue | Where-Object { $_.FriendlyName -match 'Virtual Gamepad Emulation Bus' -and $_.Status -eq 'OK' }
if ($vigem) {
    L "ViGEmBus already installed"
} else {
    $setup = Join-Path $src 'ViGEmBus_Setup_x64.exe'
    L "Installing ViGEmBus silently..."
    $p = Start-Process -FilePath $setup -ArgumentList '/exenoui', '/qn', '/norestart' -Wait -PassThru
    L "ViGEmBus setup exit code: $($p.ExitCode)"
}

# --- 4. Copy the driver ------------------------------------------------------
New-Item -ItemType Directory -Force $InstallDir | Out-Null
Copy-Item (Join-Path $src 'app\*') $InstallDir -Force
# The WinUSB package travels with the driver so it can rebind itself if
# Windows Update ever restores Microsoft's driver
New-Item -ItemType Directory -Force (Join-Path $InstallDir 'winusb-driver') | Out-Null
Copy-Item (Join-Path $src 'winusb-driver\*') (Join-Path $InstallDir 'winusb-driver') -Force
L "Copied xow-win to $InstallDir"

@"
@echo off
REM Cleanly stops xow-win (powers the controller off, releases the dongle).
powershell -NoProfile -Command "try { `$e=[System.Threading.EventWaitHandle]::OpenExisting('Local\xow-win-stop'); [void]`$e.Set(); Write-Host 'Stop signal sent.' } catch { Write-Host 'xow-win is not running.' }"
timeout /t 3 /nobreak >nul
tasklist /FI "IMAGENAME eq xow-win.exe" 2>nul | find /I "xow-win.exe" >nul && taskkill /IM xow-win.exe /F >nul 2>&1
echo Done.
"@ | Set-Content -Path (Join-Path $InstallDir 'Stop-Xbox-Wireless-Driver.cmd') -Encoding ASCII

@"
@echo off
REM Runs xow-win in a visible window (troubleshooting). Normally it already runs
REM hidden via the scheduled task; this refuses to start a second copy.
title Xbox Wireless Driver (xow-win)
cd /d "$InstallDir"
start /wait "" xow-win.exe
echo.
echo xow-win exited. Press any key to close.
pause >nul
"@ | Set-Content -Path (Join-Path $InstallDir 'Xbox-Wireless-Driver.cmd') -Encoding ASCII

# --- 5. Start at login: scheduled task, runs as the user, no window ----------
$userName = $UserName
$action = New-ScheduledTaskAction -Execute (Join-Path $InstallDir 'xow-win.exe') -Argument '--background' -WorkingDirectory $InstallDir
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $userName
# Highest: the self-healing USB port cycle and WinUSB rebind need admin rights.
# For an administrator account this runs silently at login (no UAC prompt).
$principal = New-ScheduledTaskPrincipal -UserId $userName -LogonType Interactive -RunLevel Highest
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) -Hidden -MultipleInstances IgnoreNew
Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false -ErrorAction SilentlyContinue
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Starts the xow-win user-mode driver for the Xbox Wireless Adapter at login.' | Out-Null
L "Login task '$TaskName' registered for $userName (elevated, self-healing on)"

@"
@echo off
REM Opens the controller settings (button remapping, deadzones, vibration).
REM Saved changes apply within a second while the driver runs.
if not exist "$InstallDir\xow-win.ini" echo The settings file appears after the controller connects for the first time. & pause & exit /b
start "" notepad.exe "$InstallDir\xow-win.ini"
"@ | Set-Content -Path (Join-Path $InstallDir 'Xbox-Controller-Settings.cmd') -Encoding ASCII

# Desktop shortcuts for start (visible) / stop / settings
foreach ($f in 'Xbox-Wireless-Driver.cmd', 'Stop-Xbox-Wireless-Driver.cmd', 'Xbox-Controller-Settings.cmd') {
    Copy-Item (Join-Path $InstallDir $f) (Join-Path $DesktopDir $f) -Force -ErrorAction SilentlyContinue
}

# --- 6. Start now through the task (runs as the user, not elevated) ----------
if (-not $NoStart) {
    Start-ScheduledTask -TaskName $TaskName
    Start-Sleep 8
    $running = Get-Process xow-win -ErrorAction SilentlyContinue
    L ("xow-win running: {0}" -f [bool]$running)
    $appLog = Join-Path $InstallDir 'xow-win.log'
    if (Test-Path $appLog) { Get-Content $appLog | ForEach-Object { L "  | $_" } }
}

L "=== install complete (log: $log) ==="
