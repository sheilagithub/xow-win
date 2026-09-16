/*
 * Copyright (C) 2019 Medusalix
 * Windows port changes 2026.
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

#include "controller.h"
#include "../utils/log.h"

#include <cstdlib>
#include <utility>

// Accessories use IDs greater than zero
#define DEVICE_ID_CONTROLLER 0
#define DEVICE_NAME "Xbox One Wireless Controller"

#define RUMBLE_MAX_POWER 100
#define RUMBLE_DELAY std::chrono::milliseconds(10)

Controller::Controller(
    SendPacket sendPacket
) : GipDevice(sendPacket),
    inputDevice(std::bind(
        &Controller::inputFeedbackReceived,
        this,
        std::placeholders::_1,
        std::placeholders::_2
    )),
    inputReady(false),
    stopRumbleThread(false) {}

Controller::~Controller()
{
    stopRumbleThread = true;
    rumbleCondition.notify_one();

    if (rumbleThread.joinable())
    {
        rumbleThread.join();
    }

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_OFF))
    {
        Log::error("Failed to turn off controller");
    }
}

void Controller::deviceAnnounced(uint8_t id, const AnnounceData *announce)
{
    // Controllers sometimes repeat the announce (e.g. after a resume or a
    // slow acknowledgement). Initializing twice would start a second rumble
    // thread over the first, which terminates the process.
    if (inputReady)
    {
        Log::debug("Duplicate announce ignored (product id %04x)", announce->productId);

        return;
    }

    Log::info("Device announced, product id: %04x", announce->productId);
    Log::info(
        "Firmware version: %d.%d.%d.%d",
        announce->firmwareVersion.major,
        announce->firmwareVersion.minor,
        announce->firmwareVersion.build,
        announce->firmwareVersion.revision
    );
    Log::debug(
        "Hardware version: %d.%d.%d.%d",
        announce->hardwareVersion.major,
        announce->hardwareVersion.minor,
        announce->hardwareVersion.build,
        announce->hardwareVersion.revision
    );

    initInput(announce);
}

void Controller::statusReceived(uint8_t id, const StatusData *status)
{
    const std::string levels[] = { "empty", "low", "medium", "full" };

    uint8_t type = status->batteryType;
    uint8_t level = status->batteryLevel;

    // Controller is charging or level hasn't changed
    if (type == BATT_TYPE_CHARGING || level == batteryLevel)
    {
        return;
    }

    if (level < 4)
    {
        Log::info("Battery level: %s", levels[level].c_str());
    }

    batteryLevel = level;
}

void Controller::guideButtonPressed(const GuideButtonData *button)
{
    if (!inputReady)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mappingMutex);

    guideHeld = button->pressed;

    uint16_t mask = mapping.buttons[static_cast<size_t>(Button::Guide)];

    inputDevice.setGuide(guideHeld && (mask & XUSB_GAMEPAD_GUIDE));

    // Guide remapped to a regular button: re-emit the last report with it
    if (mask & ~XUSB_GAMEPAD_GUIDE)
    {
        applyInput(lastInput);
    }
}

void Controller::refreshMapping(bool force)
{
    auto now = std::chrono::steady_clock::now();

    if (!force && now - lastMappingCheck < std::chrono::seconds(1))
    {
        return;
    }

    lastMappingCheck = now;

    std::error_code error;
    auto time = std::filesystem::last_write_time(mappingPath, error);

    if (error)
    {
        return;
    }

    if (force || time != mappingTime)
    {
        mappingTime = time;

        Mapping fresh;

        if (fresh.load(mappingPath))
        {
            mapping = fresh;
        }
    }
}

void Controller::serialNumberReceived(const SerialData *serial)
{
    const std::string number(
        serial->serialNumber,
        sizeof(serial->serialNumber)
    );

    Log::info("Serial number: %s", number.c_str());
}

void Controller::inputReceived(const InputData *input)
{
    if (!inputReady)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mappingMutex);

    refreshMapping(false);

    lastInput = *input;

    applyInput(*input);
}

// Caller holds mappingMutex
void Controller::applyInput(const InputData &input)
{
    XUSB_REPORT report = {};

    auto press = [&](bool pressed, Button physical) {
        if (pressed)
        {
            report.wButtons |= mapping.buttons[static_cast<size_t>(physical)];
        }
    };

    press(input.buttons.a, Button::A);
    press(input.buttons.b, Button::B);
    press(input.buttons.x, Button::X);
    press(input.buttons.y, Button::Y);
    press(input.buttons.bumperLeft, Button::LB);
    press(input.buttons.bumperRight, Button::RB);
    press(input.buttons.stickLeft, Button::LS);
    press(input.buttons.stickRight, Button::RS);
    press(input.buttons.start, Button::Start);
    press(input.buttons.select, Button::Back);
    press(input.buttons.dpadUp, Button::Up);
    press(input.buttons.dpadDown, Button::Down);
    press(input.buttons.dpadLeft, Button::Left);
    press(input.buttons.dpadRight, Button::Right);
    press(guideHeld, Button::Guide);

    // The guide bit itself is handled by InputDevice::setGuide
    report.wButtons &= ~XUSB_GAMEPAD_GUIDE;

    // GIP triggers are 10 bits, XInput wants 8
    uint16_t triggerLeft = input.triggerLeft;
    uint16_t triggerRight = input.triggerRight;

    if (mapping.swapTriggers)
    {
        std::swap(triggerLeft, triggerRight);
    }

    report.bLeftTrigger = mapping.applyTrigger(triggerLeft);
    report.bRightTrigger = mapping.applyTrigger(triggerRight);

    // GIP and XInput both use signed 16 bits with Y pointing up
    int16_t leftX = input.stickLeftX, leftY = input.stickLeftY;
    int16_t rightX = input.stickRightX, rightY = input.stickRightY;

    mapping.applyStickDeadzone(leftX, leftY, mapping.leftDeadzone);
    mapping.applyStickDeadzone(rightX, rightY, mapping.rightDeadzone);

    if (mapping.invertLeftY) leftY = leftY == -32768 ? 32767 : -leftY;
    if (mapping.invertRightY) rightY = rightY == -32768 ? 32767 : -rightY;

    if (mapping.swapSticks)
    {
        std::swap(leftX, rightX);
        std::swap(leftY, rightY);
    }

    report.sThumbLX = leftX;
    report.sThumbLY = leftY;
    report.sThumbRX = rightX;
    report.sThumbRY = rightY;

    inputDevice.update(report);
}

void Controller::initInput(const AnnounceData *announce)
{
    LedModeData ledMode = {};

    // Dim the LED a little bit, like the original driver
    // Brightness ranges from 0x00 to 0x20
    ledMode.mode = LED_ON;
    ledMode.brightness = 0x14;

    if (!setPowerMode(DEVICE_ID_CONTROLLER, POWER_ON))
    {
        Log::error("Failed to set initial power mode");

        return;
    }

    if (!setLedMode(ledMode))
    {
        Log::error("Failed to set initial LED mode");

        return;
    }

    if (!requestSerialNumber())
    {
        Log::error("Failed to request serial number");

        return;
    }

    InputDevice::DeviceConfig deviceConfig = {};

    deviceConfig.vendorId = announce->vendorId;
    deviceConfig.productId = announce->productId;
    deviceConfig.version = (announce->firmwareVersion.major << 8) |
        announce->firmwareVersion.minor;

    try
    {
        inputDevice.create(DEVICE_NAME, deviceConfig);
    }

    catch (const InputException &exception)
    {
        Log::error("%s", exception.what());

        return;
    }

    {
        std::lock_guard<std::mutex> lock(mappingMutex);

        mappingPath = Mapping::defaultPath();
        Mapping::writeTemplate(mappingPath);
        refreshMapping(true);
    }

    if (!rumbleThread.joinable())
    {
        rumbleThread = std::thread(&Controller::processRumble, this);
    }

    inputReady = true;
}

void Controller::processRumble()
{
    RumbleData rumble = {};
    std::unique_lock<std::mutex> lock(rumbleMutex);

    while (!stopRumbleThread)
    {
        // Wake up once a second to pick up edits to xow-win.ini even when
        // the controller is idle and sends no input
        rumbleCondition.wait_for(lock, std::chrono::seconds(1));

        {
            std::lock_guard<std::mutex> mappingLock(mappingMutex);

            refreshMapping(false);
        }

        while (rumbleBuffer.get(rumble))
        {
            performRumble(rumble);

            // Delay rumble to work around firmware bug
            std::this_thread::sleep_for(RUMBLE_DELAY);
        }
    }
}

void Controller::inputFeedbackReceived(uint8_t largeMotor, uint8_t smallMotor)
{
    Log::debug("Rumble large: %d, small: %d", largeMotor, smallMotor);

    RumbleData rumble = {};

    rumble.setRight = true;
    rumble.setLeft = true;
    rumble.setRightTrigger = true;
    rumble.setLeftTrigger = true;

    int level = 100;

    {
        std::lock_guard<std::mutex> lock(mappingMutex);

        level = mapping.vibration;
    }

    // XInput motors are 0-255, GIP wants 0-100, scaled by the vibration level
    // Large (low frequency) motor is on the left
    rumble.left = static_cast<uint32_t>(largeMotor) * RUMBLE_MAX_POWER * level / 0xff / 100;
    rumble.right = static_cast<uint32_t>(smallMotor) * RUMBLE_MAX_POWER * level / 0xff / 100;

    // XInput rumble runs until the game changes it, so make the GIP
    // effect effectively endless: 2.55 s repeated 255 times.
    // A zero-power update from the game stops it.
    rumble.duration = 0xff;
    rumble.delay = 0x00;
    rumble.repeat = (largeMotor || smallMotor) ? 0xff : 0x00;

    rumbleBuffer.put(rumble);
    rumbleCondition.notify_one();
}
