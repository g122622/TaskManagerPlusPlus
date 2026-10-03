#include "UI/Diagnostics.h"

namespace tmpp::ui::diagnostics
{
    namespace
    {
        /// The installed destination. Empty when diagnostics are off.
        std::function<void(std::string_view)> g_sink;
    }

    void SetSink(std::function<void(std::string_view)> sink)
    {
        g_sink = std::move(sink);
    }

    void Report(std::string_view message)
    {
        if (g_sink)
        {
            g_sink(message);
        }
    }

    bool Enabled() noexcept
    {
        return static_cast<bool>(g_sink);
    }
}