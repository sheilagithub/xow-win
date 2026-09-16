/*
 * xow-win: Windows port of xow
 * Self-healing helpers: USB port cycle and WinUSB rebind. Both need admin.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "recover.h"
#include "paths.h"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>

#include <cstring>
#include <fstream>
#include <sstream>

// GUID_DEVINTERFACE_USB_DEVICE / GUID_DEVINTERFACE_USB_HUB (usbiodef.h)
DEFINE_GUID(XowGuidUsbDevice, 0xA5DCBF10, 0x6530, 0x11D2, 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED);
DEFINE_GUID(XowGuidUsbHub, 0xF18A0E88, 0xC30C, 0x11D0, 0x88, 0x15, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0xD8);

// IOCTL_USB_HUB_CYCLE_PORT = CTL_CODE(FILE_DEVICE_USB 0x22, 273, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define XOW_IOCTL_USB_HUB_CYCLE_PORT 0x220444

struct XowUsbCyclePortParams
{
    ULONG connectionIndex;
    ULONG statusReturned;
};

namespace
{
    bool isAdapterId(const std::string &id)
    {
        return id.find("VID_045E&PID_02E6") != std::string::npos ||
            id.find("VID_045E&PID_02FE") != std::string::npos ||
            id.find("VID_045E&PID_091E") != std::string::npos;
    }

    std::string win32Error(DWORD code)
    {
        char *buffer = nullptr;
        DWORD length = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, reinterpret_cast<char*>(&buffer), 0, nullptr
        );
        std::string text = length ? std::string(buffer, length) : "";

        if (buffer) LocalFree(buffer);

        while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();

        std::ostringstream stream;

        stream << "error " << code << (text.empty() ? "" : " (" + text + ")");

        return stream.str();
    }
}

bool Recover::isElevated()
{
    HANDLE token = nullptr;
    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    bool elevated = false;

    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
    {
        if (GetTokenInformation(token, TokenElevation, &elevation, size, &size))
        {
            elevated = elevation.TokenIsElevated != 0;
        }

        CloseHandle(token);
    }

    return elevated;
}

std::string Recover::findAdapter()
{
    HDEVINFO set = SetupDiGetClassDevsA(&XowGuidUsbDevice, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (set == INVALID_HANDLE_VALUE)
    {
        return "";
    }

    std::string found;
    SP_DEVINFO_DATA info = {};

    info.cbSize = sizeof(info);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); i++)
    {
        char id[MAX_DEVICE_ID_LEN] = {};

        if (SetupDiGetDeviceInstanceIdA(set, &info, id, sizeof(id), nullptr) && isAdapterId(id))
        {
            found = id;

            break;
        }
    }

    SetupDiDestroyDeviceInfoList(set);

    return found;
}

bool Recover::cycleAdapterPort(std::string &detail)
{
    HDEVINFO set = SetupDiGetClassDevsA(&XowGuidUsbDevice, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (set == INVALID_HANDLE_VALUE)
    {
        detail = "SetupDiGetClassDevs: " + win32Error(GetLastError());

        return false;
    }

    SP_DEVINFO_DATA info = {};
    DWORD port = 0;
    std::string hubId;
    bool found = false;

    info.cbSize = sizeof(info);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); i++)
    {
        char id[MAX_DEVICE_ID_LEN] = {};

        if (!SetupDiGetDeviceInstanceIdA(set, &info, id, sizeof(id), nullptr) || !isAdapterId(id))
        {
            continue;
        }

        // Port number on the parent hub
        DWORD type = 0;

        if (!SetupDiGetDeviceRegistryPropertyA(set, &info, SPDRP_ADDRESS, &type, reinterpret_cast<BYTE*>(&port), sizeof(port), nullptr))
        {
            detail = "port number: " + win32Error(GetLastError());

            break;
        }

        DEVINST parent = 0;

        if (CM_Get_Parent(&parent, info.DevInst, 0) != CR_SUCCESS)
        {
            detail = "no parent hub";

            break;
        }

        char parentId[MAX_DEVICE_ID_LEN] = {};

        if (CM_Get_Device_IDA(parent, parentId, sizeof(parentId), 0) != CR_SUCCESS)
        {
            detail = "no parent hub id";

            break;
        }

        hubId = parentId;
        found = true;

        break;
    }

    SetupDiDestroyDeviceInfoList(set);

    if (!found)
    {
        if (detail.empty()) detail = "adapter not present";

        return false;
    }

    // Hub device interface path: \\?\<instance id with \ -> #>#{hub guid}
    std::string path = "\\\\?\\" + hubId + "#{f18a0e88-c30c-11d0-8815-00a0c906bed8}";

    for (char &c : path)
    {
        if (c == '\\' && &c - path.data() >= 4) c = '#';
    }

    HANDLE hub = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

    if (hub == INVALID_HANDLE_VALUE)
    {
        detail = "open hub " + hubId + ": " + win32Error(GetLastError());

        return false;
    }

    XowUsbCyclePortParams params = {};
    DWORD returned = 0;

    params.connectionIndex = port;

    bool ok = DeviceIoControl(hub, XOW_IOCTL_USB_HUB_CYCLE_PORT, &params, sizeof(params), &params, sizeof(params), &returned, nullptr) != 0;
    DWORD error = ok ? 0 : GetLastError();

    CloseHandle(hub);

    std::ostringstream stream;

    stream << "port " << port << " of " << hubId << ": " << (ok ? "cycled" : win32Error(error)) << ", status 0x" << std::hex << params.statusReturned;

    detail = stream.str();

    return ok;
}

bool Recover::rebindWinUsb(std::string &detail)
{
    std::string inf = Paths::executableDirectory() + "\\winusb-driver\\xbox_acc.inf";

    if (!std::ifstream(inf))
    {
        detail = "driver package not found: " + inf;

        return false;
    }

    typedef BOOL (WINAPI *UpdateDriverFn)(HWND, LPCSTR, LPCSTR, DWORD, PBOOL);

    HMODULE newdev = LoadLibraryA("newdev.dll");
    UpdateDriverFn update = newdev ? reinterpret_cast<UpdateDriverFn>(GetProcAddress(newdev, "UpdateDriverForPlugAndPlayDevicesA")) : nullptr;

    if (!update)
    {
        detail = "newdev.dll not available";

        return false;
    }

    const DWORD INSTALLFLAG_FORCE = 0x1;
    BOOL reboot = FALSE;
    bool ok = update(nullptr, "USB\\VID_045E&PID_02E6", inf.c_str(), INSTALLFLAG_FORCE, &reboot) != 0;
    DWORD error = ok ? 0 : GetLastError();

    detail = ok ? std::string("bound to WinUSB") + (reboot ? " (reboot required)" : "") : win32Error(error);

    return ok;
}
