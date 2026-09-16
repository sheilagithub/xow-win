# Cycles the USB port the Xbox Wireless Adapter is on (IOCTL_USB_HUB_CYCLE_PORT),
# the software equivalent of unplugging and replugging it. Needs admin.
$ErrorActionPreference = 'Continue'
$log = Join-Path $env:TEMP 'xow-win-portcycle.log'
function L($m) { $line = (Get-Date -Format 'HH:mm:ss') + '  ' + $m; Write-Host $line; Add-Content $log $line }
$dev = Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'USB\VID_045E&PID_02E6*' -or $_.InstanceId -like 'USB\VID_045E&PID_02FE*' -or $_.InstanceId -like 'USB\VID_045E&PID_091E*' } | Select-Object -First 1
if (-not $dev) { L 'adapter not present'; Start-Sleep 5; exit 1 }
$port = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName 'DEVPKEY_Device_Address').Data
$hub = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName 'DEVPKEY_Device_Parent').Data
$hubPath = '\?\' + ($hub -replace '\', '#') + '#{f18a0e88-c30c-11d0-8815-00a0c906bed8}'
L "adapter $($dev.InstanceId) on port $port of hub $hub"
Add-Type -Namespace XowWin -Name Usb -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true, CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr CreateFile(string name, uint access, uint share, System.IntPtr sa, uint disposition, uint flags, System.IntPtr template);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool DeviceIoControl(System.IntPtr h, uint code, byte[] inBuf, uint inSize, byte[] outBuf, uint outSize, out uint returned, System.IntPtr overlapped);
[System.Runtime.InteropServices.DllImport("kernel32.dll", SetLastError = true)]
public static extern bool CloseHandle(System.IntPtr h);
'@
$h = [XowWin.Usb]::CreateFile($hubPath, 0xC0000000, 3, [IntPtr]::Zero, 3, 0, [IntPtr]::Zero)
if ($h -eq [IntPtr]-1) { L ("cannot open hub: win32 error " + [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()); Start-Sleep 5; exit 1 }
$buf = New-Object byte[] 8; [BitConverter]::GetBytes([uint32]$port).CopyTo($buf, 0)
$IOCTL_USB_HUB_CYCLE_PORT = 0x220444
$ret = 0
$ok = [XowWin.Usb]::DeviceIoControl($h, $IOCTL_USB_HUB_CYCLE_PORT, $buf, 8, $buf, 8, [ref]$ret, [IntPtr]::Zero)
$err = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
[void][XowWin.Usb]::CloseHandle($h)
L ("cycle port {0}: ok={1} win32={2} status={3}" -f $port, $ok, $err, [BitConverter]::ToUInt32($buf, 4))
Start-Sleep 4
