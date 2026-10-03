// Monotonic clock, expressed without leaking a platform header.
//
// The Domain layer needs elapsed time to turn counter deltas into rates, but it
// must not include <windows.h> (see docs/ARCHITECTURE.md: Domain depends only on
// Platform interfaces and the standard library). This header is that interface;
// the Windows implementation lives in the Platform layer.
#pragma once

#include <cstdint>

namespace tmpp::platform
{
    /**
     * @brief A monotonic timestamp.
     *
     * Opaque to callers: only differences between two values are meaningful, and
     * only via MillisecondsBetween.
     */
    using Timestamp = uint64_t;

    /**
     * @brief Reads the monotonic clock.
     *
     * Monotonic, so it is unaffected by wall-clock adjustments. It does not
     * advance while the system is suspended, which is the behaviour wanted here:
     * a suspended interval should not appear as a huge sampling gap.
     */
    [[nodiscard]] Timestamp SteadyNow() noexcept;

    /**
     * @brief Elapsed milliseconds between two readings.
     *
     * Returns 0 when @p later is not after @p earlier, so callers get a zero rate
     * rather than an infinite or negative one.
     */
    [[nodiscard]] double MillisecondsBetween(Timestamp earlier, Timestamp later) noexcept;
}
