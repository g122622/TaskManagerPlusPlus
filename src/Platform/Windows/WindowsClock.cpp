#include "Platform/Clock.h"

#include <windows.h>

namespace tmpp::platform
{
    namespace
    {
        /**
         * @brief QueryPerformanceFrequency as ticks per millisecond.
         *
         * The frequency is fixed for the lifetime of the system, so it is read
         * once. A zero frequency (which should not occur on any supported
         * Windows) is reported as 0 and makes every elapsed time 0.
         */
        [[nodiscard]] uint64_t _ticksPerMillisecond() noexcept
        {
            static uint64_t const cached = []() noexcept -> uint64_t {
                LARGE_INTEGER frequency{};
                if (QueryPerformanceFrequency(&frequency) == 0 || frequency.QuadPart <= 0)
                {
                    return 0;
                }
                return static_cast<uint64_t>(frequency.QuadPart) / 1000;
            }();
            return cached;
        }
    }

    Timestamp SteadyNow() noexcept
    {
        LARGE_INTEGER counter{};
        QueryPerformanceCounter(&counter);
        return static_cast<uint64_t>(counter.QuadPart);
    }

    double MillisecondsBetween(Timestamp earlier, Timestamp later) noexcept
    {
        if (later <= earlier)
        {
            return 0.0;
        }

        uint64_t const ticksPerMs = _ticksPerMillisecond();
        if (ticksPerMs == 0)
        {
            return 0.0;
        }

        return static_cast<double>(later - earlier) / static_cast<double>(ticksPerMs);
    }
}
