// Sampling cadence and history retention: the single place these literals live.
//
// Every value here is exposed to the user as a setting, so the defaults, the
// permitted ranges and the clamping all sit together. Callers must use Clamp*
// rather than re-deriving bounds, so that a persisted out-of-range value can
// never reach the sampler.
#pragma once

#include <algorithm>
#include <cstdint>

namespace tmpp::domain
{
    namespace sampling
    {
        /// Default interval between samples, in milliseconds.
        inline constexpr uint32_t DEFAULT_INTERVAL_MS = 1000;

        /// Fastest permitted sampling interval. Going below this costs more CPU
        /// than the measurements themselves.
        inline constexpr uint32_t MIN_INTERVAL_MS = 500;

        /// Slowest user-selectable interval.
        inline constexpr uint32_t MAX_INTERVAL_MS = 5000;

        /// Interval used while the window is minimised. Sampling is not stopped
        /// outright so that the charts remain continuous when the window returns.
        inline constexpr uint32_t MINIMIZED_INTERVAL_MS = 5000;

        /// Default history window, in seconds.
        inline constexpr uint32_t DEFAULT_HISTORY_SECONDS = 60;

        /// Shortest selectable history window.
        inline constexpr uint32_t MIN_HISTORY_SECONDS = 60;

        /// Longest selectable history window.
        inline constexpr uint32_t MAX_HISTORY_SECONDS = 1800;

        /// Hard ceiling on retained points per series, independent of the selected
        /// window. With the fastest interval this bounds a 30-minute window at
        /// 3600 points; the cap keeps a pathological interval from growing it.
        inline constexpr size_t MAX_HISTORY_POINTS = 4096;

        /**
         * @brief Clamps a sampling interval to the permitted range.
         */
        [[nodiscard]] constexpr uint32_t ClampInterval(uint32_t milliseconds) noexcept
        {
            return std::clamp(milliseconds, MIN_INTERVAL_MS, MAX_INTERVAL_MS);
        }

        /**
         * @brief Clamps a history window to the permitted range.
         */
        [[nodiscard]] constexpr uint32_t ClampHistorySeconds(uint32_t seconds) noexcept
        {
            return std::clamp(seconds, MIN_HISTORY_SECONDS, MAX_HISTORY_SECONDS);
        }

        /**
         * @brief Number of samples retained for a given interval and window.
         *
         * Always at least 2 (a single point cannot draw a line) and never more
         * than MAX_HISTORY_POINTS.
         */
        [[nodiscard]] constexpr size_t HistoryCapacity(uint32_t intervalMs, uint32_t historySeconds) noexcept
        {
            uint32_t const interval = ClampInterval(intervalMs);
            uint32_t const window = ClampHistorySeconds(historySeconds);

            size_t const points = (static_cast<size_t>(window) * 1000) / interval;
            return std::clamp<size_t>(points, 2, MAX_HISTORY_POINTS);
        }
    }

    namespace limits
    {
        /**
         * @brief Largest plausible per-process CPU percentage.
         *
         * A process can consume every logical processor, so the ceiling is the
         * core count times 100. Anything above that indicates a counter glitch
         * rather than real load.
         */
        [[nodiscard]] constexpr double MaxProcessCpuPercent(uint32_t logicalProcessorCount) noexcept
        {
            return static_cast<double>(logicalProcessorCount) * 100.0;
        }

        /// Largest plausible CPU percentage for a single logical processor.
        inline constexpr double MAX_CPU_PERCENT = 100.0;

        /// Largest plausible memory-load percentage.
        inline constexpr double MAX_MEMORY_PERCENT = 100.0;
    }
}
