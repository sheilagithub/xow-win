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

#include "usb.h"
#include "../utils/log.h"

#include <chrono>
#include <thread>

// Timeouts in milliseconds
#define USB_TIMEOUT_READ 1000
#define USB_TIMEOUT_WRITE 1000

UsbDevice::UsbDevice(
    libusb_device *device,
    Terminate terminate
) : terminate(terminate)
{
    Log::debug("Opening device...");

    int error = libusb_open(device, &handle);

    if (error)
    {
        throw UsbException("Error opening device", error);
    }

    // On Windows (WinUSB) a reset only resets the pipes and may be
    // reported as unsupported; that is not fatal.
    error = libusb_reset_device(handle);

    if (error)
    {
        Log::debug("Device reset not performed: %s", libusb_error_name(error));
    }

    int configuration = 0;

    error = libusb_get_configuration(handle, &configuration);

    if (error || configuration != 1)
    {
        error = libusb_set_configuration(handle, 1);

        if (error)
        {
            Log::debug(
                "Could not set configuration: %s",
                libusb_error_name(error)
            );
        }
    }

    error = libusb_claim_interface(handle, 0);

    if (error)
    {
        throw UsbException("Error claiming interface", error);
    }
}

UsbDevice::~UsbDevice()
{
    Log::debug("Closing device...");

    int error = libusb_release_interface(handle, 0);

    if (error)
    {
        Log::error(
            "Error releasing interface: %s",
            libusb_error_name(error)
        );
    }

    libusb_close(handle);
}

void UsbDevice::controlTransfer(ControlPacket packet, bool write)
{
    uint8_t direction = write ? LIBUSB_ENDPOINT_OUT : LIBUSB_ENDPOINT_IN;

    // Number of bytes or error code
    int transferred = libusb_control_transfer(
        handle,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | direction,
        packet.request,
        packet.value,
        packet.index,
        packet.data,
        packet.length,
        USB_TIMEOUT_WRITE
    );

    if (transferred != packet.length)
    {
        Log::error(
            "Error in control transfer: %s",
            libusb_error_name(transferred)
        );

        terminate();
    }
}

int UsbDevice::bulkRead(
    uint8_t endpoint,
    FixedBytes<USB_MAX_BULK_TRANSFER_SIZE> &buffer
) {
    int transferred = 0;
    int error = libusb_bulk_transfer(
        handle,
        endpoint | LIBUSB_ENDPOINT_IN,
        buffer.raw(),
        buffer.size(),
        &transferred,
        USB_TIMEOUT_READ
    );

    if (error && error != LIBUSB_ERROR_TIMEOUT)
    {
        Log::error("Error in bulk read: %s", libusb_error_name(error));

        terminate();

        return -1;
    }

    return transferred;
}

bool UsbDevice::bulkWrite(uint8_t endpoint, Bytes &data)
{
    int transferred = 0;
    int error = libusb_bulk_transfer(
        handle,
        endpoint | LIBUSB_ENDPOINT_OUT,
        data.raw(),
        data.size(),
        &transferred,
        USB_TIMEOUT_WRITE
    );

    if (error)
    {
        Log::error("Error in bulk write: %s", libusb_error_name(error));

        terminate();

        return false;
    }

    return true;
}

UsbDeviceManager::UsbDeviceManager()
{
    int error = libusb_init(nullptr);

    if (error)
    {
        throw UsbException("Error initializing libusb", error);
    }
}

UsbDeviceManager::~UsbDeviceManager()
{
    libusb_exit(nullptr);
}

libusb_device* UsbDeviceManager::findDevice(const std::vector<HardwareId> &ids)
{
    libusb_device **list = nullptr;
    ssize_t count = libusb_get_device_list(nullptr, &list);

    if (count < 0)
    {
        throw UsbException("Error listing devices", static_cast<int>(count));
    }

    libusb_device *found = nullptr;

    for (ssize_t i = 0; i < count && !found; i++)
    {
        libusb_device_descriptor descriptor = {};

        if (libusb_get_device_descriptor(list[i], &descriptor))
        {
            continue;
        }

        for (const HardwareId &id : ids)
        {
            if (
                descriptor.idVendor == id.vendorId &&
                descriptor.idProduct == id.productId
            ) {
                found = libusb_ref_device(list[i]);

                break;
            }
        }
    }

    libusb_free_device_list(list, 1);

    return found;
}

std::unique_ptr<UsbDevice> UsbDeviceManager::getDevice(
    std::initializer_list<HardwareId> ids,
    UsbDevice::Terminate terminate,
    std::function<bool()> shouldStop,
    std::function<void(int)> onOpenFailure
) {
    const std::vector<HardwareId> wanted(ids);
    bool announced = false;
    int openFailures = 0;

    while (!shouldStop())
    {
        libusb_device *device = findDevice(wanted);

        if (device)
        {
            try
            {
                std::unique_ptr<UsbDevice> result(new UsbDevice(device, terminate));

                libusb_unref_device(device);

                return result;
            }

            catch (const UsbException &exception)
            {
                libusb_unref_device(device);

                // Typical cause: the dongle is still bound to the Microsoft
                // driver instead of WinUSB, or another xow-win is running
                openFailures++;

                Log::error("%s", exception.what());
                Log::info("Is the adapter bound to WinUSB? Retrying in 3 s...");

                if (onOpenFailure)
                {
                    onOpenFailure(openFailures);
                }

                for (int i = 0; i < 30 && !shouldStop(); i++)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                continue;
            }
        }

        if (!announced)
        {
            Log::info("Waiting for the Xbox Wireless Adapter...");

            announced = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    return nullptr;
}

UsbException::UsbException(
    std::string message,
    int error
) : std::runtime_error(message + ": " + libusb_error_name(error)) {}
