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

#pragma once

#include "gip.h"
#include "input.h"
#include "mapping.h"
#include "../utils/buffer.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>

/*
 * Forwards gamepad events to the virtual controller
 * Passes rumble requests from games to the gamepad
 */
class Controller : public GipDevice
{
public:
    Controller(SendPacket sendPacket);
    ~Controller();

private:
    /* GIP events */
    void deviceAnnounced(uint8_t id, const AnnounceData *announce) override;
    void statusReceived(uint8_t id, const StatusData *status) override;
    void guideButtonPressed(const GuideButtonData *button) override;
    void serialNumberReceived(const SerialData *serial) override;
    void inputReceived(const InputData *input) override;

    /* Device initialization */
    void initInput(const AnnounceData *announce);

    /* Rumble buffer consumer */
    void processRumble();

    /* OS interface */
    void inputFeedbackReceived(uint8_t largeMotor, uint8_t smallMotor);

    /* Settings file (xow-win.ini), re-read when it changes */
    void refreshMapping(bool force);
    void applyInput(const InputData &input);

    Mapping mapping;
    std::string mappingPath;
    std::filesystem::file_time_type mappingTime = {};
    std::chrono::steady_clock::time_point lastMappingCheck = {};
    std::mutex mappingMutex;

    InputData lastInput = {};
    bool guideHeld = false;

    InputDevice inputDevice;
    std::atomic<bool> inputReady;
    std::atomic<bool> stopRumbleThread;
    std::thread rumbleThread;
    std::mutex rumbleMutex;
    std::condition_variable rumbleCondition;
    Buffer<RumbleData> rumbleBuffer;

    uint8_t batteryLevel = 0xff;
};
