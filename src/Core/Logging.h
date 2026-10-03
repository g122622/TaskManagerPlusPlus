// Logging facade.
//
// Every translation unit includes this instead of <spdlog/spdlog.h> directly, for
// three reasons:
//
//   1. spdlog pulls in fmt, whose recent versions emit deprecation warnings from
//      their own headers at /W4. Wrapping the include here keeps those warnings
//      out of the project without suppressing the diagnostic globally, which would
//      also hide real deprecations in our own code.
//
//   2. The project's logging policy (see docs/CODE_CONVENTIONS.md) permits info
//      and above only: trace and debug are compiled out because they cost more
//      than the measurements this application exists to take.
//
//   3. Initialisation belongs in one place, so the level and sink are decided once.
#pragma once

// Third-party headers are not held to this project's warning level.
#pragma warning(push)
#pragma warning(disable : 4996) // fmt's own deprecated fstring overloads
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/spdlog.h>
#pragma warning(pop)

#include <memory>

namespace tmpp::core::log
{
    /**
     * @brief Initialises logging for the application.
     *
     * @param verbose When true, info-level diagnostics are emitted in addition to
     *        warnings and errors. Release builds pass false so the shipped
     *        application is silent unless something is wrong.
     */
    void Initialize(bool verbose);

    /**
     * @brief Flushes and releases logging resources.
     */
    void Shutdown();
}
