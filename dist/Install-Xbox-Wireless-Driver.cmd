@echo off
REM xow-win silent installer. Double-click, approve the single admin prompt.
REM Installs: WinUSB binding for the Xbox Wireless Adapter, ViGEmBus, the driver
REM in %LOCALAPPDATA%\xow-win, a login entry, and starts it.
set "INSTALLDIR=%LOCALAPPDATA%\xow-win"
set "STARTUPDIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "DESKTOPDIR=%USERPROFILE%\Desktop"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%~dp0install.ps1\"','-InstallDir','\"%INSTALLDIR%\"','-StartupDir','\"%STARTUPDIR%\"','-DesktopDir','\"%DESKTOPDIR%\"','-UserName','\"%USERDOMAIN%\%USERNAME%\"'"
echo.
if not exist "%TEMP%\xow-win-install.log" (
    echo The install did not run: the administrator prompt was declined or timed out.
    echo Run this file again and click Yes on the prompt.
) else (
    echo Install finished. Log: %TEMP%\xow-win-install.log
    type "%TEMP%\xow-win-install.log" | findstr /C:"Adapter driver service" /C:"ViGEmBus" /C:"Login task" /C:"xow-win running" /C:"ERROR"
)
echo.
pause
