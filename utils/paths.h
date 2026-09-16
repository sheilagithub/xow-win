/*
 * xow-win: Windows port of xow
 * Locates files relative to the executable.
 */

#pragma once

#include <string>
#include <windows.h>

namespace Paths
{
    inline std::string executablePath()
    {
        char buffer[MAX_PATH] = {};
        DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);

        return std::string(buffer, length);
    }

    inline std::string executableDirectory()
    {
        std::string path = executablePath();
        size_t slash = path.find_last_of("\\/");

        return slash == std::string::npos ? "." : path.substr(0, slash);
    }

    inline const std::string& firmware()
    {
        static const std::string path = executableDirectory() + "\\FW_ACC_00U.bin";

        return path;
    }
}
