// Startup diagnostics for the TaskManagerPlusPlus application.
//
// WinUI reports most startup failures as a stowed exception (0xC000027B), which
// carries no message and bypasses the C++ exception machinery. This logger
// records the startup stage reached so a failure can be localised to a specific
// step.
//
// The whole facility compiles to nothing in release builds: it is a diagnostic
// aid, not runtime telemetry.
#pragma once

#include <windows.h>

#if defined(_DEBUG)

#include <cstdio>
#include <string>

namespace tmpp::diag
{
    /**
     * @brief Appends one line to startup.log next to the executable.
     *
     * @param message Text to record. ASCII only; startup diagnostics are not
     *                localized.
     */
    inline void LogStartup(char const* message)
    {
        wchar_t exePath[MAX_PATH]{};
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0)
        {
            return;
        }

        std::wstring logPath{exePath};
        auto const slash = logPath.find_last_of(L'\\');
        if (slash == std::wstring::npos)
        {
            return;
        }
        logPath.resize(slash + 1);
        logPath += L"startup.log";

        FILE* file = nullptr;
        if (_wfopen_s(&file, logPath.c_str(), L"a") != 0 || file == nullptr)
        {
            return;
        }

        std::fputs(message, file);
        std::fputs("\n", file);
        std::fflush(file);
        std::fclose(file);
    }

    /**
     * @brief Records a stage together with an HRESULT and detail message.
     */
    inline void LogStartupError(char const* stage, HRESULT hr, char const* detail)
    {
        char buffer[1024]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "[error] %s hr=0x%08X %s",
                      stage,
                      static_cast<unsigned int>(hr),
                      detail != nullptr ? detail : "");
        LogStartup(buffer);
    }
}

#else  // Release build: diagnostics compile away.

namespace tmpp::diag
{
    inline void LogStartup(char const*) {}

    inline void LogStartupError(char const*, HRESULT, char const*) {}
}

#endif
