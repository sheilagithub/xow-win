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

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <stdexcept>
#include <mutex>

#include <windows.h>
#include <ViGEm/Client.h>

/*
 * Virtual Xbox 360 controller exposed through the ViGEmBus driver.
 * Games see it through XInput. Rumble requests from the game are
 * passed back through the feedback callback.
 */
class InputDevice
{
public:
    using FeedbackReceived = std::function<void(
        uint8_t largeMotor,
        uint8_t smallMotor
    )>;

    struct DeviceConfig
    {
        uint16_t vendorId, productId;
        uint16_t version;
    };

    InputDevice(FeedbackReceived feedbackReceived);
    ~InputDevice();

    void create(std::string name, DeviceConfig config);

    // Full state update, sent to the bus immediately
    void update(const XUSB_REPORT &report);

    // Guide button lives outside the regular input report in GIP
    void setGuide(bool pressed);

private:
    static VOID CALLBACK notification(
        PVIGEM_CLIENT client,
        PVIGEM_TARGET target,
        UCHAR largeMotor,
        UCHAR smallMotor,
        UCHAR ledNumber,
        LPVOID userData
    );

    PVIGEM_TARGET target = nullptr;
    bool attached = false;

    std::mutex reportMutex;
    XUSB_REPORT lastReport = {};
    bool guidePressed = false;

    FeedbackReceived feedbackReceived;
};

/*
 * Process-wide connection to the ViGEmBus driver
 */
class ViGEmBus
{
public:
    static PVIGEM_CLIENT client();

private:
    static PVIGEM_CLIENT instance;
    static std::mutex instanceMutex;
};

class InputException : public std::runtime_error
{
public:
    InputException(std::string message);
    InputException(std::string message, VIGEM_ERROR error);
};
