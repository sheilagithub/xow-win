/*
 * xow-win: Windows port of xow (https://github.com/medusalix/xow)
 * Copyright (C) 2019 Medusalix (original xow)
 * Windows input backend (ViGEm) added 2026.
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
 */

#include "input.h"
#include "../utils/log.h"

#include <sstream>

PVIGEM_CLIENT ViGEmBus::instance = nullptr;
std::mutex ViGEmBus::instanceMutex;

static std::string vigemErrorName(VIGEM_ERROR error)
{
    switch (error)
    {
        case VIGEM_ERROR_NONE: return "success";
        case VIGEM_ERROR_BUS_NOT_FOUND: return "ViGEmBus driver not found (is it installed?)";
        case VIGEM_ERROR_NO_FREE_SLOT: return "no free slot";
        case VIGEM_ERROR_INVALID_TARGET: return "invalid target";
        case VIGEM_ERROR_REMOVAL_FAILED: return "removal failed";
        case VIGEM_ERROR_ALREADY_CONNECTED: return "already connected";
        case VIGEM_ERROR_TARGET_UNINITIALIZED: return "target uninitialized";
        case VIGEM_ERROR_TARGET_NOT_PLUGGED_IN: return "target not plugged in";
        case VIGEM_ERROR_BUS_VERSION_MISMATCH: return "bus version mismatch";
        case VIGEM_ERROR_BUS_ACCESS_FAILED: return "bus access failed";
        case VIGEM_ERROR_CALLBACK_ALREADY_REGISTERED: return "callback already registered";
        case VIGEM_ERROR_CALLBACK_NOT_FOUND: return "callback not found";
        case VIGEM_ERROR_BUS_ALREADY_CONNECTED: return "bus already connected";
        case VIGEM_ERROR_BUS_INVALID_HANDLE: return "bus invalid handle";
        case VIGEM_ERROR_XUSB_USERINDEX_OUT_OF_RANGE: return "user index out of range";
        case VIGEM_ERROR_INVALID_PARAMETER: return "invalid parameter";
        case VIGEM_ERROR_NOT_SUPPORTED: return "not supported";
        case VIGEM_ERROR_WINAPI: return "Win32 API error";
        case VIGEM_ERROR_TIMED_OUT: return "timed out";
        case VIGEM_ERROR_IS_DISPOSING: return "disposing";
    }

    std::ostringstream stream;

    stream << "error 0x" << std::hex << static_cast<uint32_t>(error);

    return stream.str();
}

PVIGEM_CLIENT ViGEmBus::client()
{
    std::lock_guard<std::mutex> lock(instanceMutex);

    if (instance)
    {
        return instance;
    }

    PVIGEM_CLIENT client = vigem_alloc();

    if (!client)
    {
        throw InputException("Error allocating ViGEm client");
    }

    VIGEM_ERROR error = vigem_connect(client);

    if (!VIGEM_SUCCESS(error))
    {
        vigem_free(client);

        throw InputException("Error connecting to ViGEmBus", error);
    }

    Log::info("Connected to ViGEmBus");

    instance = client;

    return instance;
}

InputDevice::InputDevice(
    FeedbackReceived feedbackReceived
) : feedbackReceived(feedbackReceived) {}

InputDevice::~InputDevice()
{
    if (!target)
    {
        return;
    }

    if (attached)
    {
        vigem_target_x360_unregister_notification(target);

        VIGEM_ERROR error = vigem_target_remove(ViGEmBus::client(), target);

        if (!VIGEM_SUCCESS(error))
        {
            Log::error(
                "Error removing virtual controller: %s",
                vigemErrorName(error).c_str()
            );
        }
    }

    vigem_target_free(target);
}

void InputDevice::create(std::string name, DeviceConfig config)
{
    PVIGEM_CLIENT client = ViGEmBus::client();

    target = vigem_target_x360_alloc();

    if (!target)
    {
        throw InputException("Error allocating virtual controller");
    }

    // Keep the stock Xbox 360 IDs: that is what XInput-only games expect.
    // The real controller's IDs are only logged.
    Log::info(
        "Creating virtual Xbox 360 controller for '%s' (%04x:%04x v%04x)",
        name.c_str(),
        config.vendorId,
        config.productId,
        config.version
    );

    VIGEM_ERROR error = vigem_target_add(client, target);

    if (!VIGEM_SUCCESS(error))
    {
        throw InputException("Error plugging in virtual controller", error);
    }

    attached = true;

    error = vigem_target_x360_register_notification(
        client,
        target,
        &InputDevice::notification,
        this
    );

    if (!VIGEM_SUCCESS(error))
    {
        Log::error(
            "Rumble will not work: %s",
            vigemErrorName(error).c_str()
        );
    }

    ULONG index = 0;

    if (VIGEM_SUCCESS(vigem_target_x360_get_user_index(client, target, &index)))
    {
        Log::info("Virtual controller ready as player %lu", index + 1);
    }
}

void InputDevice::update(const XUSB_REPORT &report)
{
    if (!attached)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(reportMutex);

    lastReport = report;

    if (guidePressed)
    {
        lastReport.wButtons |= XUSB_GAMEPAD_GUIDE;
    }

    else
    {
        lastReport.wButtons &= ~XUSB_GAMEPAD_GUIDE;
    }

    VIGEM_ERROR error = vigem_target_x360_update(
        ViGEmBus::client(),
        target,
        lastReport
    );

    if (!VIGEM_SUCCESS(error))
    {
        Log::error(
            "Error updating virtual controller: %s",
            vigemErrorName(error).c_str()
        );
    }
}

void InputDevice::setGuide(bool pressed)
{
    XUSB_REPORT report;

    {
        std::lock_guard<std::mutex> lock(reportMutex);

        guidePressed = pressed;
        report = lastReport;
    }

    update(report);
}

VOID CALLBACK InputDevice::notification(
    PVIGEM_CLIENT client,
    PVIGEM_TARGET target,
    UCHAR largeMotor,
    UCHAR smallMotor,
    UCHAR ledNumber,
    LPVOID userData
) {
    InputDevice *device = static_cast<InputDevice*>(userData);

    if (device && device->feedbackReceived)
    {
        device->feedbackReceived(largeMotor, smallMotor);
    }
}

InputException::InputException(
    std::string message
) : std::runtime_error(message) {}

InputException::InputException(
    std::string message,
    VIGEM_ERROR error
) : std::runtime_error(message + ": " + vigemErrorName(error)) {}
