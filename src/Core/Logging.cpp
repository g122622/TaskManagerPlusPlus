#include "Core/Logging.h"

namespace tmpp::core::log
{
    namespace
    {
        bool g_initialized{false};
    }

    void Initialize(bool verbose)
    {
        if (g_initialized)
        {
            return;
        }

        // The application writes no log file: it is a desktop monitor, not a
        // service, and leaving files behind in its directory would be surprising.
        // Messages go to the debugger output, which is where a developer looks.
        spdlog::set_default_logger(std::make_shared<spdlog::logger>("tmpp", std::make_shared<spdlog::sinks::msvc_sink_mt>()));

        // trace and debug are excluded by policy: they cost more than the
        // measurements this application takes. In release only warnings and errors
        // are emitted, so a correctly functioning run produces no output at all.
        spdlog::set_level(verbose ? spdlog::level::info : spdlog::level::warn);
        spdlog::flush_on(spdlog::level::warn);

        // Include the source location so a warning points at the call site.
        spdlog::set_pattern("[%^%l%$] %s:%# %v");

        g_initialized = true;
    }

    void Shutdown()
    {
        if (!g_initialized)
        {
            return;
        }

        spdlog::shutdown();
        g_initialized = false;
    }
}
