// Counter-to-rate arithmetic.
//
// Everything here is a pure function of raw counters and elapsed time. No system
// calls, no allocation, no hidden state: this is where the interesting edge cases
// live (counter rollback, PID reuse, division by zero, clock going backwards),
// so it must be trivially unit-testable.
#pragma once

#include <cstdint>

#include "Domain/SamplingConfig.h"

namespace tmpp::domain
{
    /**
     * @brief Outcome of differencing two consecutive cumulative counter readings.
     *
     * A negative delta is not an error to swallow: it means the counter went
     * backwards, which happens when a PID is recycled, when a process restarts,
     * or when a 32-bit counter wraps. Callers must decide what to display, and
     * the reason is carried alongside the value so that decision is informed.
     */
    enum class DeltaStatus
    {
        Ok,           ///< Both readings are usable and the delta is non-negative.
        NoBaseline,   ///< No previous reading exists yet.
        CounterReset, ///< The new reading is lower than the previous one.
    };

    /**
     * @brief Result of a counter difference.
     */
    struct Delta
    {
        uint64_t value{0};
        DeltaStatus status{DeltaStatus::NoBaseline};

        [[nodiscard]] bool Usable() const noexcept { return status == DeltaStatus::Ok; }
    };

    /**
     * @brief Differences two cumulative counter readings.
     *
     * @param previous Earlier reading.
     * @param current  Later reading.
     * @return The non-negative delta, or a status explaining why it is unusable.
     */
    [[nodiscard]] constexpr Delta ComputeDelta(uint64_t previous, uint64_t current) noexcept
    {
        if (current < previous)
        {
            return Delta{0, DeltaStatus::CounterReset};
        }
        return Delta{current - previous, DeltaStatus::Ok};
    }

    /**
     * @brief Number of 100 ns intervals in a millisecond.
     */
    inline constexpr uint64_t TICKS_PER_MILLISECOND = 10000;

    /**
     * @brief Converts a millisecond duration to 100 ns intervals.
     */
    [[nodiscard]] constexpr uint64_t MillisecondsToTicks(uint64_t milliseconds) noexcept
    {
        return milliseconds * TICKS_PER_MILLISECOND;
    }

    /**
     * @brief Converts 100 ns intervals to milliseconds.
     */
    [[nodiscard]] constexpr uint64_t TicksToMilliseconds(uint64_t ticks) noexcept
    {
        return ticks / TICKS_PER_MILLISECOND;
    }

    /**
     * @brief A percentage in the range [0, ceiling].
     */
    struct Percentage
    {
        double value{0.0};

        /// True when the raw ratio was outside [0, ceiling] and was clamped,
        /// or when the inputs made the ratio undefined. Callers use this to
        /// decide whether to log, not to change what is displayed.
        bool clamped{false};
    };

    /**
     * @brief Computes part/whole as a percentage, clamped to [0, ceiling].
     *
     * Returns 0 with @c clamped set when @p whole is 0, rather than dividing by
     * zero. That case is real: the first sample of a freshly started system, or
     * a system tick that produced no CPU time at all.
     */
    [[nodiscard]] constexpr Percentage ComputePercentage(uint64_t part,
                                                         uint64_t whole,
                                                         double ceiling = limits::MAX_CPU_PERCENT) noexcept
    {
        if (whole == 0)
        {
            return Percentage{0.0, true};
        }

        double const raw = (static_cast<double>(part) * 100.0) / static_cast<double>(whole);

        if (raw < 0.0)
        {
            return Percentage{0.0, true};
        }
        if (raw > ceiling)
        {
            return Percentage{ceiling, true};
        }
        return Percentage{raw, false};
    }

    /**
     * @brief Computes a per-second rate from a counter delta and a duration.
     *
     * @param deltaValue Counter units accumulated over the interval.
     * @param elapsedMs  Interval length in milliseconds.
     * @return Units per second, or 0 when the interval is zero.
     */
    [[nodiscard]] constexpr double ComputeRatePerSecond(uint64_t deltaValue, uint64_t elapsedMs) noexcept
    {
        if (elapsedMs == 0)
        {
            return 0.0;
        }
        return (static_cast<double>(deltaValue) * 1000.0) / static_cast<double>(elapsedMs);
    }

    /**
     * @brief A process's share of total system CPU, expressed so that one fully
     *        busy logical processor is 100%.
     *
     * The caller supplies the already-computed deltas. CPU time on Windows is
     * split into kernel and user; both count towards the process's consumption.
     *
     * @param processDelta   Change in the process's total CPU time, in 100 ns ticks.
     * @param systemDelta    Change in total system CPU time (busy + idle), same units.
     * @param logicalProcessors Number of logical processors, used to scale the
     *        result so that it matches Task Manager's convention.
     * @return Percentage in [0, logicalProcessors * 100].
     */
    [[nodiscard]] constexpr Percentage ComputeProcessCpuPercent(uint64_t processDelta,
                                                                uint64_t systemDelta,
                                                                uint32_t logicalProcessors) noexcept
    {
        if (systemDelta == 0 || logicalProcessors == 0)
        {
            return Percentage{0.0, true};
        }

        // systemDelta spans all processors, so multiply the process's share by the
        // processor count to normalise against a single processor's capacity.
        double const ceiling = limits::MaxProcessCpuPercent(logicalProcessors);
        double const raw =
            (static_cast<double>(processDelta) * 100.0 * static_cast<double>(logicalProcessors)) /
            static_cast<double>(systemDelta);

        if (raw < 0.0)
        {
            return Percentage{0.0, true};
        }
        if (raw > ceiling)
        {
            return Percentage{ceiling, true};
        }
        return Percentage{raw, false};
    }

    /**
     * @brief System-wide CPU usage as a percentage over the interval.
     *
     * Windows reports kernel time *including* idle time, so idle must be
     * subtracted before the busy fraction is taken. Getting this wrong makes an
     * idle machine look heavily loaded, which is why the subtraction lives here
     * rather than at the call site.
     *
     * @param busyDelta Total busy time over the interval, in 100 ns ticks.
     * @param idleDelta Idle time over the interval, in 100 ns ticks.
     */
    [[nodiscard]] constexpr Percentage ComputeSystemCpuPercent(uint64_t busyDelta, uint64_t idleDelta) noexcept
    {
        uint64_t const total = busyDelta + idleDelta;
        if (total == 0)
        {
            return Percentage{0.0, true};
        }
        return ComputePercentage(busyDelta, total, limits::MAX_CPU_PERCENT);
    }
}
