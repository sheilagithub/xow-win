xow-win  -  user-mode driver for the Xbox Wireless Adapter for Windows
=====================================================================

Replaces Microsoft's adapter driver stack (mt7612US.sys + xboxgip.sys) with a
small program that talks to the dongle directly and presents the controller to
games as an Xbox 360 pad. If the dongle wedges, the program resets it. No reboots.

INSTALL (one admin prompt, otherwise silent)
  1. Windows Security > App & browser control > Smart App Control > Off.
     (Only on PCs that have it on. It blocks all unsigned programs. One-way switch.)
  2. Plug in the adapter.
  3. Double-click Install-Xbox-Wireless-Driver.cmd and approve the prompt.

  What it does:
   - obtains the dongle firmware (FW_ACC_00U.bin, Microsoft's, not included):
     from this PC's driver store if Microsoft's adapter driver was ever
     installed, otherwise by downloading Microsoft's driver package
   - trusts and installs the WinUSB driver package for the adapter (the same one
     Zadig generates) and force-binds the adapter to it
   - installs ViGEmBus 1.22 silently (virtual controller driver, signed)
   - copies the driver to %LOCALAPPDATA%\xow-win
   - adds xow-win.vbs to the Startup folder so it runs hidden at login
   - puts Xbox-Wireless-Driver.cmd (visible window) and
     Stop-Xbox-Wireless-Driver.cmd on the Desktop
   - starts it

USE
  Turn the controller on. A controller paired to this dongle before rejoins by
  itself. To pair a new one: press the dongle button, then hold the controller's
  pair button. Log file: %LOCALAPPDATA%\xow-win\xow-win.log

UNINSTALL
  Double-click Uninstall-Xbox-Wireless-Driver.cmd. Microsoft's driver comes back
  automatically. ViGEmBus is left installed.

IF WINDOWS UPDATE PUTS MICROSOFT'S DRIVER BACK
  The log will say the adapter is not bound to WinUSB. Re-run the installer
  (or use zadig.exe by hand: Options > List All Devices > Xbox Wireless Adapter
  > WinUSB > Replace Driver).

FILES
  app\               xow-win.exe + FW_ACC_00U.bin (dongle firmware from Microsoft's driver)
  winusb-driver\     xbox_acc.inf, XBOX_ACC.cat, libwdi-xbox-acc.cer (from Zadig/libwdi)
  ViGEmBus_Setup_x64.exe, zadig.exe (fallback)
  install.ps1 / uninstall.ps1 and their .cmd launchers

Source and build instructions: https://github.com/medusalix/xow (original, Linux)
and the xow-win folder this was built from. License GPL-2.0 (xow), MIT (ViGEmClient).
