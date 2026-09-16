/*
 * xow-win: Windows port of xow
 * Controller remapping / settings, read from xow-win.ini next to the exe.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include "mapping.h"
#include "../utils/log.h"
#include "../utils/paths.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>

namespace
{
    std::string trim(const std::string &s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");

        return a == std::string::npos ? "" : s.substr(a, b - a + 1);
    }

    std::string lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        return s;
    }

    bool parseBool(const std::string &v, bool &out)
    {
        std::string s = lower(v);

        if (s == "1" || s == "true" || s == "yes" || s == "on") { out = true; return true; }
        if (s == "0" || s == "false" || s == "no" || s == "off") { out = false; return true; }

        return false;
    }

    bool parseFloat(const std::string &v, float &out)
    {
        try
        {
            size_t used = 0;
            float f = std::stof(v, &used);

            if (used != v.size()) return false;

            out = f;

            return true;
        }

        catch (...)
        {
            return false;
        }
    }

    // Accepts "0.15" or "15%"
    bool parsePercentOrFraction(const std::string &v, float &out)
    {
        std::string s = trim(v);

        if (!s.empty() && s.back() == '%')
        {
            float f = 0;

            if (!parseFloat(trim(s.substr(0, s.size() - 1)), f)) return false;

            out = f / 100.0f;
        }

        else if (!parseFloat(s, out))
        {
            return false;
        }

        out = std::clamp(out, 0.0f, 0.95f);

        return true;
    }

    const std::map<std::string, Button> physicalNames = {
        { "a", Button::A }, { "b", Button::B }, { "x", Button::X }, { "y", Button::Y },
        { "lb", Button::LB }, { "rb", Button::RB },
        { "ls", Button::LS }, { "rs", Button::RS },
        { "start", Button::Start }, { "menu", Button::Start },
        { "back", Button::Back }, { "view", Button::Back }, { "select", Button::Back },
        { "up", Button::Up }, { "down", Button::Down }, { "left", Button::Left }, { "right", Button::Right },
        { "guide", Button::Guide }, { "xbox", Button::Guide },
    };

    const std::map<std::string, uint16_t> virtualNames = {
        { "a", XUSB_GAMEPAD_A }, { "b", XUSB_GAMEPAD_B }, { "x", XUSB_GAMEPAD_X }, { "y", XUSB_GAMEPAD_Y },
        { "lb", XUSB_GAMEPAD_LEFT_SHOULDER }, { "rb", XUSB_GAMEPAD_RIGHT_SHOULDER },
        { "ls", XUSB_GAMEPAD_LEFT_THUMB }, { "rs", XUSB_GAMEPAD_RIGHT_THUMB },
        { "start", XUSB_GAMEPAD_START }, { "menu", XUSB_GAMEPAD_START },
        { "back", XUSB_GAMEPAD_BACK }, { "view", XUSB_GAMEPAD_BACK }, { "select", XUSB_GAMEPAD_BACK },
        { "up", XUSB_GAMEPAD_DPAD_UP }, { "down", XUSB_GAMEPAD_DPAD_DOWN },
        { "left", XUSB_GAMEPAD_DPAD_LEFT }, { "right", XUSB_GAMEPAD_DPAD_RIGHT },
        { "guide", XUSB_GAMEPAD_GUIDE }, { "xbox", XUSB_GAMEPAD_GUIDE },
        { "none", 0 }, { "off", 0 }, { "disabled", 0 },
    };
}

Mapping::Mapping()
{
    // Identity mapping
    buttons[static_cast<size_t>(Button::A)] = XUSB_GAMEPAD_A;
    buttons[static_cast<size_t>(Button::B)] = XUSB_GAMEPAD_B;
    buttons[static_cast<size_t>(Button::X)] = XUSB_GAMEPAD_X;
    buttons[static_cast<size_t>(Button::Y)] = XUSB_GAMEPAD_Y;
    buttons[static_cast<size_t>(Button::LB)] = XUSB_GAMEPAD_LEFT_SHOULDER;
    buttons[static_cast<size_t>(Button::RB)] = XUSB_GAMEPAD_RIGHT_SHOULDER;
    buttons[static_cast<size_t>(Button::LS)] = XUSB_GAMEPAD_LEFT_THUMB;
    buttons[static_cast<size_t>(Button::RS)] = XUSB_GAMEPAD_RIGHT_THUMB;
    buttons[static_cast<size_t>(Button::Start)] = XUSB_GAMEPAD_START;
    buttons[static_cast<size_t>(Button::Back)] = XUSB_GAMEPAD_BACK;
    buttons[static_cast<size_t>(Button::Up)] = XUSB_GAMEPAD_DPAD_UP;
    buttons[static_cast<size_t>(Button::Down)] = XUSB_GAMEPAD_DPAD_DOWN;
    buttons[static_cast<size_t>(Button::Left)] = XUSB_GAMEPAD_DPAD_LEFT;
    buttons[static_cast<size_t>(Button::Right)] = XUSB_GAMEPAD_DPAD_RIGHT;
    buttons[static_cast<size_t>(Button::Guide)] = XUSB_GAMEPAD_GUIDE;
}

std::string Mapping::defaultPath()
{
    return Paths::executableDirectory() + "\\xow-win.ini";
}

Button Mapping::buttonFromName(const std::string &name, bool &ok)
{
    auto it = physicalNames.find(lower(trim(name)));

    ok = it != physicalNames.end();

    return ok ? it->second : Button::Count;
}

uint16_t Mapping::buttonMaskFromName(const std::string &name, bool &ok)
{
    auto it = virtualNames.find(lower(trim(name)));

    ok = it != virtualNames.end();

    return ok ? it->second : 0;
}

bool Mapping::load(const std::string &path)
{
    std::ifstream file(path);

    if (!file)
    {
        return false;
    }

    Mapping fresh;
    std::string line, section;
    int lineNumber = 0, applied = 0;

    while (std::getline(file, line))
    {
        lineNumber++;

        // Strip comments
        size_t comment = line.find_first_of(";#");

        if (comment != std::string::npos)
        {
            line = line.substr(0, comment);
        }

        line = trim(line);

        if (line.empty())
        {
            continue;
        }

        if (line.front() == '[' && line.back() == ']')
        {
            section = lower(trim(line.substr(1, line.size() - 2)));

            continue;
        }

        size_t eq = line.find('=');

        if (eq == std::string::npos)
        {
            Log::error("xow-win.ini line %d: expected key = value", lineNumber);

            continue;
        }

        std::string key = lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        bool ok = false;

        if (section == "buttons")
        {
            Button physical = buttonFromName(key, ok);

            if (!ok)
            {
                Log::error("xow-win.ini line %d: unknown button '%s'", lineNumber, key.c_str());

                continue;
            }

            uint16_t mask = buttonMaskFromName(value, ok);

            if (!ok)
            {
                Log::error("xow-win.ini line %d: unknown target '%s'", lineNumber, value.c_str());

                continue;
            }

            fresh.buttons[static_cast<size_t>(physical)] = mask;
            applied++;
        }

        else if (section == "sticks")
        {
            if (key == "left_deadzone") ok = parsePercentOrFraction(value, fresh.leftDeadzone);
            else if (key == "right_deadzone") ok = parsePercentOrFraction(value, fresh.rightDeadzone);
            else if (key == "invert_left_y") ok = parseBool(value, fresh.invertLeftY);
            else if (key == "invert_right_y") ok = parseBool(value, fresh.invertRightY);
            else if (key == "swap_sticks") ok = parseBool(value, fresh.swapSticks);
            else { Log::error("xow-win.ini line %d: unknown setting '%s'", lineNumber, key.c_str()); continue; }

            if (!ok) { Log::error("xow-win.ini line %d: bad value for '%s'", lineNumber, key.c_str()); continue; }

            applied++;
        }

        else if (section == "triggers")
        {
            if (key == "deadzone") ok = parsePercentOrFraction(value, fresh.triggerDeadzone);
            else if (key == "swap_triggers") ok = parseBool(value, fresh.swapTriggers);
            else { Log::error("xow-win.ini line %d: unknown setting '%s'", lineNumber, key.c_str()); continue; }

            if (!ok) { Log::error("xow-win.ini line %d: bad value for '%s'", lineNumber, key.c_str()); continue; }

            applied++;
        }

        else if (section == "vibration")
        {
            if (key == "level")
            {
                float f = 0;
                std::string s = value;

                if (!s.empty() && s.back() == '%') s.pop_back();

                ok = parseFloat(trim(s), f);

                if (!ok) { Log::error("xow-win.ini line %d: bad value for 'level'", lineNumber); continue; }

                fresh.vibration = static_cast<int>(std::clamp(f, 0.0f, 100.0f));
                applied++;
            }

            else { Log::error("xow-win.ini line %d: unknown setting '%s'", lineNumber, key.c_str()); }
        }

        else
        {
            Log::error("xow-win.ini line %d: unknown section '%s'", lineNumber, section.c_str());
        }
    }

    *this = fresh;

    Log::info("Settings loaded from xow-win.ini (%d entries)", applied);

    return true;
}

void Mapping::applyStickDeadzone(int16_t &x, int16_t &y, float deadzone) const
{
    if (deadzone <= 0.0f)
    {
        return;
    }

    float fx = x / 32767.0f;
    float fy = y / 32767.0f;
    float magnitude = std::sqrt(fx * fx + fy * fy);

    if (magnitude < deadzone)
    {
        x = 0;
        y = 0;

        return;
    }

    // Rescale so the range past the deadzone still reaches full deflection
    float scale = (std::min(magnitude, 1.0f) - deadzone) / (1.0f - deadzone) / magnitude;

    x = static_cast<int16_t>(std::clamp(fx * scale * 32767.0f, -32768.0f, 32767.0f));
    y = static_cast<int16_t>(std::clamp(fy * scale * 32767.0f, -32768.0f, 32767.0f));
}

uint8_t Mapping::applyTrigger(uint16_t value) const
{
    float f = std::min(value, static_cast<uint16_t>(1023)) / 1023.0f;

    if (triggerDeadzone > 0.0f)
    {
        f = f < triggerDeadzone ? 0.0f : (f - triggerDeadzone) / (1.0f - triggerDeadzone);
    }

    return static_cast<uint8_t>(std::clamp(f * 255.0f, 0.0f, 255.0f));
}

void Mapping::writeTemplate(const std::string &path)
{
    std::ifstream exists(path);

    if (exists)
    {
        return;
    }

    std::ofstream file(path);

    file <<
        "; xow-win controller settings. Edit and save: changes apply within a second.\n"
        "; Lines starting with ; are comments. Everything here is optional.\n"
        "\n"
        "[buttons]\n"
        "; physical = virtual   Buttons: a b x y lb rb ls rs start back up down left right guide\n"
        "; Use 'none' to disable a button. Examples:\n"
        ";   a = b\n"
        ";   b = a\n"
        ";   rs = none\n"
        "\n"
        "[sticks]\n"
        "; Deadzone as a fraction or percent of full deflection (0 = off). Try 10% if a stick drifts.\n"
        "left_deadzone = 0\n"
        "right_deadzone = 0\n"
        "invert_left_y = false\n"
        "invert_right_y = false\n"
        "swap_sticks = false\n"
        "\n"
        "[triggers]\n"
        "deadzone = 0\n"
        "swap_triggers = false\n"
        "\n"
        "[vibration]\n"
        "; 0 = off, 100 = full\n"
        "level = 100\n";
}
