/*
 * xow-win: Windows port of xow
 * Self-healing helpers: USB port cycle and WinUSB rebind. Both need admin.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <string>

namespace Recover
{
    bool isElevated();

    // Instance ID of the first present Xbox Wireless Adapter, "" if none
    std::string findAdapter();

    // Software equivalent of unplugging and replugging the adapter:
    // asks the parent hub to cycle the adapter's port (re-enumeration)
    bool cycleAdapterPort(std::string &detail);

    // Force-binds the adapter to the WinUSB driver package shipped in
    // <exe dir>\winusb-driver\xbox_acc.inf (what Zadig does)
    bool rebindWinUsb(std::string &detail);
}
