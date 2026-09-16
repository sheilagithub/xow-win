# xow-win

Windows port of [xow](https://github.com/medusalix/xow), the user-mode driver for
the Xbox Wireless Adapter (first- and second-generation dongles). It replaces
Microsoft's `mt7612US.sys` + `xboxgip.sys` stack with a normal program:

    dongle  <-USB (WinUSB + libusb)->  xow-win.exe  <-ViGEmBus->  virtual Xbox 360 controller  ->  games (XInput)

Nothing here runs in the kernel; the only drivers involved are Microsoft's WinUSB
and the signed ViGEmBus. If the dongle ever wedges, the program resets and
re-initializes it by itself. No reboot.

## Daily use

- It runs hidden at login via the scheduled task the installer registers (dist\Install-Xbox-Wireless-Driver.cmd). Log: `build\xow-win.log`.
- `Desktop\Xbox-Wireless-Driver.cmd`: run it in a visible window instead (troubleshooting).
- `Desktop\Stop-Xbox-Wireless-Driver.cmd`: stop it cleanly (controller powers off).
- Only one copy can run at a time; a second one exits with "already running".
- Pair a controller: press the dongle button (light blinks), then hold the
  controller's pair button. A controller that was paired before rejoins on its own.

## One-time setup on a fresh PC

1. Windows Security > App & browser control > Smart App Control: Off
   (it blocks all unsigned programs, including this one; one-way switch).
2. Install ViGEmBus (`ViGEmBus_Setup_x64.exe`), the virtual controller driver.
3. Bind the adapter to WinUSB with Zadig (`zadig.exe`): Options > List All Devices,
   pick "Xbox Wireless Adapter for Windows" (USB ID 045E 02E6), target WinUSB,
   Replace Driver. Reversible: Device Manager > the adapter > Update driver >
   Browse > Let me pick > "Xbox Wireless Adapter for Windows".
4. Or just run dist\Install-Xbox-Wireless-Driver.cmd, which does steps 2-3 silently and registers the login task.

## Self-healing

`xow-win.exe --background` (what the login task runs) is a supervisor that spawns
`xow-win.exe --worker --background` and restarts it if it dies (2 s backoff,
doubling to 60 s; the reason is logged). The worker:

- releases the dongle when Windows goes to sleep and reconnects after resume
- after 3 failed dongle initializations, cycles the adapter's USB port
  (`IOCTL_USB_HUB_CYCLE_PORT`, the software equivalent of a replug)
- after 3 failed opens, re-applies the WinUSB binding from `winusb-driver\`
  (in case Windows Update restored Microsoft's driver)
- ignores a repeated controller announce (the cause of the 2026-09-16 crash)

The USB parts need admin, so the installer registers the task with RunLevel
Highest. Maintenance from an elevated prompt: `xow-win.exe --cycle-port`,
`xow-win.exe --rebind`. Logs: `xow-win.log` (current run), `xow-win.prev.log`.

## Controller settings (xow-win.ini)

Created next to the exe the first time a controller connects; `Xbox-Controller-Settings.cmd`
on the Desktop opens it. Saved changes apply within a second, no restart.

    [buttons]     physical = virtual, e.g. a = b / b = a / rs = none
                  names: a b x y lb rb ls rs start back up down left right guide
    [sticks]      left_deadzone, right_deadzone (0 or e.g. 10%), invert_left_y,
                  invert_right_y, swap_sticks
    [triggers]    deadzone, swap_triggers
    [vibration]   level = 0..100

## Files not in this repository

- `firmware/FW_ACC_00U.bin`: Microsoft's proprietary dongle firmware. Run
  `tools\Get-Firmware.ps1` to copy it from a PC's driver store (or download
  Microsoft's package), then place it next to `xow-win.exe`.
- `dist\ViGEmBus_Setup_x64.exe`: https://github.com/nefarius/ViGEmBus/releases (v1.22.0)
- `dist\zadig.exe`: https://github.com/pbatard/libwdi/releases (fallback only)
- `build\` and `dist\app\xow-win.exe`: build output (`mingw32-make`)

## Build

    mingw32-make            # build/xow-win.exe
    mingw32-make debug      # build/xow-win-debug.exe, logs every radio frame

Toolchain: MinGW-w64 (WinLibs) via `winget install BrechtSanders.WinLibs.POSIX.UCRT`.
`FW_ACC_00U.bin` must sit next to the exe. Use `firmware/FW_ACC_00U_2015.bin`
(from Microsoft's driver 21.50.45.656 for the 02E6 dongle). The 2017 blob in
`firmware/` (from the 02FE package, the one Linux xow downloads) hung this dongle
mid-upload; kept for reference only.

## Porting notes

- `-mno-ms-bitfields` is mandatory for the xow code. MinGW's default MS bit-field
  layout pads xow's packed 16-bit `FrameControl` (declared with `uint32_t` fields)
  to 4 bytes and shifts every 802.11 header field by two bytes: beacons go out
  malformed and the controller never associates. `Dongle::Dongle` has
  `static_assert`s that fail the build if the layout is wrong.
- `ViGEmClient.cpp` is compiled with the MS layout because it talks to Win32.
- libusb hotplug is not used; the device is polled every 500 ms.
- `libusb_reset_device` and `libusb_set_configuration` failures are non-fatal on
  WinUSB.

## Layout

- `dongle/`      MT76 radio + firmware loader, dongle packet handling (from xow)
- `controller/`  GIP protocol (from xow); `input.*`, `controller.*` ported to ViGEm
- `utils/`       byte buffers, logging (from xow); `paths.h` (new)
- `main.cpp`     reconnect loop, Ctrl+C, `--background` file logging, stop event, single instance
- `third_party/` libusb 1.0.30 static lib, ViGEmClient (MIT)

License: GPL-2.0 (xow), MIT (ViGEmClient).
