/*
 * Copyright (C) 2019 Medusalix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include "../utils/bytes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <stdexcept>

#include <vector>
#include <libusb.h>

#define USB_MAX_BULK_TRANSFER_SIZE 512

/*
 * Base class for interfacing with USB devices
 * Provides control/bulk transfer capabilities
 */
class UsbDevice
{
public:
    using Terminate = std::function<void()>;

    struct ControlPacket
    {
        uint8_t request;
        uint16_t value;
        uint16_t index;
        uint8_t *data;
        uint16_t length;
    };

    UsbDevice(libusb_device *device, Terminate terminate);
    virtual ~UsbDevice();

    void controlTransfer(ControlPacket packet, bool write);
    int bulkRead(
        uint8_t endpoint,
        FixedBytes<USB_MAX_BULK_TRANSFER_SIZE> &buffer
    );
    bool bulkWrite(uint8_t endpoint, Bytes &data);

private:
    libusb_device_handle *handle;
    Terminate terminate;
};

/*
 * Provides access to USB devices
 * Handles device enumeration and hot plugging
 */
class UsbDeviceManager
{
public:
    struct HardwareId
    {
        uint16_t vendorId, productId;
    };

    UsbDeviceManager();
    ~UsbDeviceManager();

    // Polls until a matching device is present (or shouldStop returns true,
    // in which case nullptr is returned). Windows libusb hotplug support is
    // still patchy, so polling is used instead of hotplug callbacks.
    // onOpenFailure(n) is called after the n-th consecutive failure to open
    // a present device (typically: bound to the wrong driver)
    std::unique_ptr<UsbDevice> getDevice(
        std::initializer_list<HardwareId> ids,
        UsbDevice::Terminate terminate,
        std::function<bool()> shouldStop,
        std::function<void(int)> onOpenFailure = nullptr
    );

private:
    libusb_device* findDevice(const std::vector<HardwareId> &ids);
};

class UsbException : public std::runtime_error
{
public:
    UsbException(std::string message, int error);
};
