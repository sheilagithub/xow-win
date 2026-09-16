@echo off
REM Cleanly stops xow-win (powers the controller off, releases the dongle).
powershell -NoProfile -Command "try { $e=[System.Threading.EventWaitHandle]::OpenExisting('Local\xow-win-stop'); [void]$e.Set(); Write-Host 'Stop signal sent.' } catch { Write-Host 'xow-win is not running.' }"
timeout /t 3 /nobreak >nul
tasklist /FI "IMAGENAME eq xow-win.exe" 2>nul | find /I "xow-win.exe" >nul && (echo Still running, forcing... & taskkill /IM xow-win.exe /F >nul 2>&1)
tasklist /FI "IMAGENAME eq xow-win-debug.exe" 2>nul | find /I "xow-win-debug.exe" >nul && taskkill /IM xow-win-debug.exe /F >nul 2>&1
echo Done.
