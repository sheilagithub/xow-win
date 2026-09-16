@echo off
REM Removes xow-win and gives the Xbox Wireless Adapter back to Microsoft's driver.
set "INSTALLDIR=%LOCALAPPDATA%\xow-win"
set "STARTUPDIR=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "DESKTOPDIR=%USERPROFILE%\Desktop"
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%~dp0uninstall.ps1\"','-InstallDir','\"%INSTALLDIR%\"','-StartupDir','\"%STARTUPDIR%\"','-DesktopDir','\"%DESKTOPDIR%\"'"
echo Done.
pause
