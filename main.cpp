/*
 * xow-win: Windows port of xow (https://github.com/medusalix/xow)
 * Copyright (C) 2019 Medusalix (original xow)
 * Windows port 2026.
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

/*
 * Process model
 *
 *   xow-win.exe --background          supervisor (what the login task runs):
 *                                     rotates the log, spawns the worker,
 *                                     restarts it if it dies
 *   xow-win.exe --worker --background worker: drives the dongle
 *   xow-win.exe                       interactive: worker with a console
 *   xow-win.exe --cycle-port          maintenance: replug the adapter in software
 *   xow-win.exe --rebind              maintenance: force the WinUSB binding
 *
 * Self-healing (worker, needs admin for the USB parts):
 *   - dongle won't initialize 3x in a row  -> USB port cycle
 *   - adapter can't be opened 3x in a row  -> WinUSB rebind
 *   - system sleep                          -> release dongle, reconnect on resume
 *   - worker crash                          -> supervisor restarts it
 */

#include "utils/log.h"
#include "utils/paths.h"
#include "utils/recover.h"
#include "dongle/usb.h"
#include "dongle/dongle.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>
#include <io.h>

// Sleep/resume notification API (Windows 8+). MinGW's headers lack these
// declarations, so they are declared here and the functions are loaded
// from powrprof.dll at runtime.
typedef ULONG (CALLBACK *XowPowerCallback)(PVOID context, ULONG type, PVOID setting);

struct XowPowerSubscribeParameters
{
    XowPowerCallback callback;
    PVOID context;
};

#define XOW_DEVICE_NOTIFY_CALLBACK 2

typedef DWORD (WINAPI *XowPowerRegisterFn)(DWORD flags, HANDLE recipient, PVOID *handle);
typedef DWORD (WINAPI *XowPowerUnregisterFn)(PVOID handle);

// Names shared with the stop script and the single-instance guards
#define INSTANCE_MUTEX_NAME "Local\\xow-win-instance"
#define WORKER_MUTEX_NAME "Local\\xow-win-worker"
#define STOP_EVENT_NAME "Local\\xow-win-stop"

// Set by Ctrl+C / window close / stop script
static std::atomic<bool> stopRequested(false);

// Set while the system is asleep: the dongle is released before suspend
// and re-opened after resume (a transfer left in flight across sleep
// wedges the dongle's firmware until it is power-cycled)
static std::atomic<bool> suspended(false);

// Signalled by the USB layer when the dongle stops answering
struct DongleWatch
{
    std::mutex mutex;
    std::condition_variable condition;
    bool lost = false;

    void lostDongle()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);

            lost = true;
        }

        condition.notify_all();
    }

    // Returns true when the dongle was lost, false when a stop was requested
    bool wait()
    {
        std::unique_lock<std::mutex> lock(mutex);

        while (!lost && !stopRequested)
        {
            condition.wait_for(lock, std::chrono::milliseconds(200));
        }

        return lost;
    }
};

static DongleWatch *activeWatch = nullptr;

static void requestStop(const char *reason)
{
    Log::info("Stop requested (%s)", reason);
    stopRequested = true;

    if (activeWatch)
    {
        activeWatch->condition.notify_all();
    }
}

static ULONG CALLBACK powerCallback(PVOID context, ULONG type, PVOID setting)
{
    if (type == PBT_APMSUSPEND)
    {
        Log::info("System going to sleep: releasing the dongle");
        suspended = true;

        if (activeWatch)
        {
            activeWatch->lostDongle();
        }
    }

    else if (type == PBT_APMRESUMEAUTOMATIC || type == PBT_APMRESUMESUSPEND)
    {
        if (suspended)
        {
            Log::info("System resumed: reconnecting the dongle");
            suspended = false;
        }
    }

    return 0;
}

static BOOL WINAPI consoleHandler(DWORD type)
{
    switch (type)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            requestStop("console");

            // Give the main thread a moment to power the controller off
            if (type == CTRL_CLOSE_EVENT)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            }

            return TRUE;
    }

    return FALSE;
}

// Waits for the named stop event (set by Stop-Xbox-Wireless-Driver.cmd)
static void stopEventThread(HANDLE stopEvent)
{
    if (WaitForSingleObject(stopEvent, INFINITE) == WAIT_OBJECT_0)
    {
        requestStop("stop script");
    }
}

// Crash reporting: make sure the reason lands in the log before we die
static void onTerminate()
{
    std::exception_ptr current = std::current_exception();

    if (current)
    {
        try { std::rethrow_exception(current); }
        catch (const std::exception &e) { Log::error("FATAL: unhandled exception: %s", e.what()); }
        catch (...) { Log::error("FATAL: unhandled exception of unknown type"); }
    }

    else
    {
        Log::error("FATAL: std::terminate called (thread misuse or noexcept violation)");
    }

    fflush(stdout);
    fflush(stderr);
    std::abort();
}

static LONG WINAPI onUnhandledException(EXCEPTION_POINTERS *info)
{
    DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;

    Log::error("FATAL: exception 0x%08lX", static_cast<unsigned long>(code));
    fflush(stdout);
    fflush(stderr);

    return EXCEPTION_EXECUTE_HANDLER;
}

static void sleepInterruptible(int tenths)
{
    for (int i = 0; i < tenths && !stopRequested && !suspended; i++)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

/*
 * Worker: drives the dongle. Returns the process exit code.
 */
static int runWorker(bool background)
{
    std::set_terminate(onTerminate);
    SetUnhandledExceptionFilter(onUnhandledException);

    HANDLE workerMutex = CreateMutexA(nullptr, FALSE, WORKER_MUTEX_NAME);

    if (workerMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        Log::error("xow-win is already running. Use Stop-Xbox-Wireless-Driver.cmd first.");

        return EXIT_FAILURE;
    }

    // Manual-reset event the stop script signals
    HANDLE stopEvent = CreateEventA(nullptr, TRUE, FALSE, STOP_EVENT_NAME);

    if (stopEvent)
    {
        std::thread(stopEventThread, stopEvent).detach();
    }

    bool elevated = Recover::isElevated();

    Log::info(
        "Self-healing: crash restart %s, USB port cycle %s, WinUSB rebind %s",
        background ? "on" : "off (interactive)",
        elevated ? "on" : "off (not admin)",
        elevated ? "on" : "off (not admin)"
    );

    if (!background)
    {
        Log::info("Press Ctrl+C to stop. Press the button on the adapter to pair.");
    }

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    XowPowerSubscribeParameters powerParams = { powerCallback, nullptr };
    PVOID powerNotify = nullptr;
    XowPowerUnregisterFn powerUnregister = nullptr;
    HMODULE powrprof = LoadLibraryA("powrprof.dll");

    if (powrprof)
    {
        auto powerRegister = reinterpret_cast<XowPowerRegisterFn>(
            GetProcAddress(powrprof, "PowerRegisterSuspendResumeNotification")
        );
        powerUnregister = reinterpret_cast<XowPowerUnregisterFn>(
            GetProcAddress(powrprof, "PowerUnregisterSuspendResumeNotification")
        );

        if (
            !powerRegister ||
            powerRegister(XOW_DEVICE_NOTIFY_CALLBACK, &powerParams, &powerNotify) != ERROR_SUCCESS
        ) {
            Log::error("Could not register for sleep/resume notifications");
            powerNotify = nullptr;
        }
    }

    // Don't let the console window's quick-edit mode freeze the process
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;

    if (input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &mode))
    {
        SetConsoleMode(input, (mode | ENABLE_EXTENDED_FLAGS) & ~ENABLE_QUICK_EDIT_MODE);
    }

    int result = EXIT_SUCCESS;

    try
    {
        UsbDeviceManager manager;
        int initFailures = 0;
        bool hintShown = false;

        // The adapter can't be opened: after a few tries, if it is bound to
        // the wrong driver (e.g. Windows Update restored Microsoft's), rebind
        auto onOpenFailure = [&](int failures) {
            if (failures != 3)
            {
                return;
            }

            if (!elevated)
            {
                Log::error("Cannot open the adapter. If Windows reinstalled Microsoft's driver, re-run the installer.");

                return;
            }

            std::string detail;
            bool ok = Recover::rebindWinUsb(detail);

            Log::info("Self-healing: WinUSB rebind %s: %s", ok ? "done" : "failed", detail.c_str());
        };

        while (!stopRequested)
        {
            // Asleep: wait for the resume notification, then give the USB
            // stack a few seconds to re-enumerate the dongle
            if (suspended)
            {
                while (suspended && !stopRequested)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }

                for (int i = 0; i < 30 && !stopRequested; i++)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                initFailures = 0;
            }

            DongleWatch watch;

            activeWatch = &watch;

            UsbDevice::Terminate terminate = std::bind(
                &DongleWatch::lostDongle,
                &watch
            );

            std::unique_ptr<UsbDevice> device = manager.getDevice({
                { DONGLE_VID, DONGLE_PID_OLD },
                { DONGLE_VID, DONGLE_PID_NEW },
                { DONGLE_VID, DONGLE_PID_SURFACE }
            }, terminate, [] { return stopRequested.load(); }, onOpenFailure);

            if (!device)
            {
                break;
            }

            int retryDelay = 30;

            try
            {
                Dongle dongle(std::move(device));

                initFailures = 0;
                hintShown = false;

                if (!watch.wait())
                {
                    // Stop requested: dongle destructor powers controllers off
                    break;
                }

                if (suspended)
                {
                    // Destructor below closes the dongle before the bus goes down
                    retryDelay = 0;
                }

                else
                {
                    Log::error("Dongle stopped responding, reinitializing...");
                }
            }

            catch (const std::exception &exception)
            {
                initFailures++;

                Log::error("Dongle error: %s", exception.what());

                // Firmware won't come up: the software equivalent of a replug
                if (initFailures % 3 == 0 && elevated)
                {
                    std::string detail;
                    bool ok = Recover::cycleAdapterPort(detail);

                    Log::info("Self-healing: USB port cycle %s: %s", ok ? "requested" : "failed", detail.c_str());

                    // Give the adapter time to drop off and come back
                    retryDelay = 80;
                }

                else if (initFailures >= 3 && !hintShown)
                {
                    Log::error("The dongle is not recovering. Unplug it and plug it back in.");
                    hintShown = true;
                    retryDelay = 100;
                }

                else
                {
                    retryDelay = initFailures >= 3 ? 100 : 30;
                }

                Log::info("Retrying in %d seconds...", retryDelay / 10);
            }

            activeWatch = nullptr;

            sleepInterruptible(retryDelay);
        }
    }

    catch (const std::exception &exception)
    {
        Log::error("Fatal: %s", exception.what());

        result = EXIT_FAILURE;
    }

    activeWatch = nullptr;

    Log::info("Shutting down...");

    if (powerNotify && powerUnregister)
    {
        powerUnregister(powerNotify);
    }

    if (stopEvent)
    {
        CloseHandle(stopEvent);
    }

    if (workerMutex)
    {
        CloseHandle(workerMutex);
    }

    return result;
}

/*
 * Supervisor: keeps a worker running until stopped. Returns the exit code.
 */
static int runSupervisor()
{
    HANDLE stopEvent = CreateEventA(nullptr, TRUE, FALSE, STOP_EVENT_NAME);

    if (stopEvent)
    {
        // May still be signalled from stopping the previous instance
        ResetEvent(stopEvent);
    }

    std::string self = Paths::executablePath();
    std::string commandLine = "\"" + self + "\" --worker --background";
    int backoffSeconds = 2;

    while (true)
    {
        STARTUPINFOA startup = {};
        PROCESS_INFORMATION process = {};
        std::string mutableCommand = commandLine;

        startup.cb = sizeof(startup);

        auto started = std::chrono::steady_clock::now();

        if (!CreateProcessA(nullptr, &mutableCommand[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
        {
            Log::error("Supervisor: cannot start worker (error %lu), retrying in 10 s", GetLastError());

            if (stopEvent && WaitForSingleObject(stopEvent, 10000) == WAIT_OBJECT_0)
            {
                break;
            }

            continue;
        }

        CloseHandle(process.hThread);

        HANDLE handles[2] = { process.hProcess, stopEvent };
        DWORD waited = WaitForMultipleObjects(stopEvent ? 2 : 1, handles, FALSE, INFINITE);

        if (waited == WAIT_OBJECT_0 + 1)
        {
            // Stop requested: the worker sees the same event and exits cleanly
            if (WaitForSingleObject(process.hProcess, 8000) != WAIT_OBJECT_0)
            {
                TerminateProcess(process.hProcess, 1);
            }

            CloseHandle(process.hProcess);
            Log::info("Supervisor: stopped");

            break;
        }

        DWORD exitCode = 0;

        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hProcess);

        auto ran = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();

        if (exitCode == 0)
        {
            Log::info("Supervisor: worker exited normally");

            break;
        }

        if (ran >= 120)
        {
            backoffSeconds = 2;
        }

        Log::error(
            "Supervisor: worker died with code 0x%08lX after %lld s, restarting in %d s",
            static_cast<unsigned long>(exitCode),
            static_cast<long long>(ran),
            backoffSeconds
        );

        if (stopEvent && WaitForSingleObject(stopEvent, backoffSeconds * 1000) == WAIT_OBJECT_0)
        {
            break;
        }

        backoffSeconds = backoffSeconds < 60 ? backoffSeconds * 2 : 60;
    }

    if (stopEvent)
    {
        CloseHandle(stopEvent);
    }

    return EXIT_SUCCESS;
}

static int runMaintenance(const char *what)
{
    std::string detail;
    bool ok = false;

    if (!Recover::isElevated())
    {
        Log::error("%s needs administrator rights (right-click > Run as administrator)", what);

        return EXIT_FAILURE;
    }

    if (std::strcmp(what, "--cycle-port") == 0)
    {
        ok = Recover::cycleAdapterPort(detail);
        Log::info("USB port cycle %s: %s", ok ? "requested" : "failed", detail.c_str());
    }

    else
    {
        ok = Recover::rebindWinUsb(detail);
        Log::info("WinUSB rebind %s: %s", ok ? "done" : "failed", detail.c_str());
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
    bool background = false;
    bool worker = false;
    const char *maintenance = nullptr;

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "--background") == 0) background = true;
        else if (std::strcmp(argv[i], "--worker") == 0) worker = true;
        else if (std::strcmp(argv[i], "--cycle-port") == 0 || std::strcmp(argv[i], "--rebind") == 0) maintenance = argv[i];
    }

    // This is a windowless (GUI subsystem) program so that the scheduled task
    // at login shows nothing. In background mode log to a file next to the
    // executable; otherwise attach to the console we were started from (or
    // open one) so the output is visible.
    if (background)
    {
        std::string logPath = Paths::executableDirectory() + "\\xow-win.log";

        if (!worker)
        {
            // Supervisor: keep the previous run's log so a crash is never lost
            std::string prevPath = Paths::executableDirectory() + "\\xow-win.prev.log";

            DeleteFileA(prevPath.c_str());
            MoveFileA(logPath.c_str(), prevPath.c_str());
        }

        // Supervisor and worker append to the same file
        freopen(logPath.c_str(), "a", stdout);

        // A windowless process starts with no usable stderr: give it the
        // log file, then make it share stdout's descriptor so INFO and
        // ERROR lines don't overwrite each other
        freopen(logPath.c_str(), "a", stderr);
        _dup2(_fileno(stdout), _fileno(stderr));
    }

    else
    {
        // If output was redirected (xow-win.exe > log.txt) the handles are
        // already valid: leave them alone. Otherwise we have no console yet.
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

        if (out == nullptr || out == INVALID_HANDLE_VALUE)
        {
            if (!AttachConsole(ATTACH_PARENT_PROCESS))
            {
                AllocConsole();
            }

            freopen("CONOUT$", "w", stdout);
            freopen("CONOUT$", "w", stderr);
            freopen("CONIN$", "r", stdin);
        }
    }

    Log::init();

    if (maintenance)
    {
        return runMaintenance(maintenance);
    }

    if (worker)
    {
        Log::info("xow-win %s worker started", VERSION);
        Log::info("Firmware: %s", Paths::firmware().c_str());

        return runWorker(true);
    }

    Log::info("xow-win %s (Windows port of xow by Severin v. W.)", VERSION);

    // Only one instance may own the dongle
    HANDLE instanceMutex = CreateMutexA(nullptr, FALSE, INSTANCE_MUTEX_NAME);

    if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        Log::error("xow-win is already running. Use Stop-Xbox-Wireless-Driver.cmd first.");

        return EXIT_FAILURE;
    }

    int result;

    if (background)
    {
        result = runSupervisor();
    }

    else
    {
        Log::info("Firmware: %s", Paths::firmware().c_str());

        HANDLE stopEvent = CreateEventA(nullptr, TRUE, FALSE, STOP_EVENT_NAME);

        if (stopEvent)
        {
            ResetEvent(stopEvent);
            CloseHandle(stopEvent);
        }

        result = runWorker(false);
    }

    if (instanceMutex)
    {
        CloseHandle(instanceMutex);
    }

    return result;
}
