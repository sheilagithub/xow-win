/*
 * xow-win: Windows port of xow
 * Controller remapping / settings, read from xow-win.ini next to the exe.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include <windows.h>
#include <ViGEm/Common.h>

/*
 * Physical buttons on the controller, in GIP order
 */
enum class Button : uint8_t
{
    A, B, X, Y,
    LB, RB,
    LS, RS,
    Start, Back,
    Up, Down, Left, Right,
    Guide,
    Count
};

/*
 * Settings applied between the controller and the virtual pad
 */
class Mapping
{
public:
    static constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::Count);

    // Virtual XUSB button mask each physical button produces (0 = disabled)
    std::array<uint16_t, BUTTON_COUNT> buttons;

    float leftDeadzone = 0.0f;
    float rightDeadzone = 0.0f;
    bool invertLeftY = false;
    bool invertRightY = false;
    bool swapSticks = false;

    float triggerDeadzone = 0.0f;
    bool swapTriggers = false;

    // 0-100
    int vibration = 100;

    Mapping();

    // Loads the file; unknown or malformed lines are logged and skipped.
    // Returns false if the file does not exist (defaults stay in place).
    bool load(const std::string &path);

    // Path of the settings file next to the executable
    static std::string defaultPath();

    // Writes the documented default file if it does not exist yet
    static void writeTemplate(const std::string &path);

    // Applies deadzone to a signed 16-bit stick axis pair (radial)
    void applyStickDeadzone(int16_t &x, int16_t &y, float deadzone) const;

    // Applies deadzone and rescale to a 10-bit trigger, returns 8-bit
    uint8_t applyTrigger(uint16_t value) const;

    static uint16_t buttonMaskFromName(const std::string &name, bool &ok);
    static Button buttonFromName(const std::string &name, bool &ok);
};
