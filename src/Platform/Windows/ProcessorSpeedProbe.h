// Live processor clock speed via the PDH performance counter API.
//
// PDH is the only practical way to read the current effective clock: the Win32
// processor information structures report topology and cumulative time, but not the
// frequency the cores are actually running at. Task Manager's "Speed" figure comes
// from the same counter family.
//
// The counter is \Processor Information(_Total)\% Processor Performance, which is a
// percentage of the nominal frequency rather than a frequency itself. It is converted
// here using the rated clock from the registry, so the value this class returns is a
// frequency in MHz and the caller never has to know about the ratio.
//
// The counter can be missing on some systems, and can transiently fail to sample on
// the first call after being added (there is no previous reading to difference
// against). Both cases are reported as "unavailable" rather than as a fabricated
// figure, because a CPU speed of 0 MHz would be worse than an em dash.
#pragma once

#include <cstdint>
#include <string>

#include "Platform/Result.h"
#include "Platform/SystemTypes.h"

namespace tmpp::platform
{
    /**
     * @brief Reads the processor's current effective clock speed.
     *
     * Not thread-safe; the sampling thread owns it.
     */
    class ProcessorSpeedProbe
    {
    public:
        /**
         * @param baseClockMhz Rated clock, used to convert the counter's percentage
         *        into a frequency. When zero the probe cannot produce a value and
         *        reports unavailable.
         */
        explicit ProcessorSpeedProbe(uint32_t baseClockMhz);
        ~ProcessorSpeedProbe();

        // Owns a PDH query handle; copying it would double-close the handle.
        ProcessorSpeedProbe(ProcessorSpeedProbe const&) = delete;
        ProcessorSpeedProbe& operator=(ProcessorSpeedProbe const&) = delete;

        /**
         * @brief Samples the counter.
         *
         * @return The current speed. A successful Result with available == false
         *         means the counter exists but has no usable reading yet, which is
         *         normal on the first call. An Error means the counter could not be
         *         set up at all.
         */
        [[nodiscard]] Result<SystemProcessorSpeed> Read();

        /// True when the counter was opened successfully.
        [[nodiscard]] bool Available() const noexcept { return m_query != nullptr && m_counter != nullptr; }

    private:
        /// Opens the query and adds the counter. Called from the constructor.
        void _open();

        /// Closes the query if it is open.
        void _close() noexcept;

        std::string m_counterPath;

        /// PDH handles, kept as void* so this header does not pull in pdh.h and its
        /// windows.h dependency. The Domain layer never sees this class directly, but
        /// keeping windows.h out of a Platform header keeps the include graph tidy.
        void* m_query{nullptr};
        void* m_counter{nullptr};

        uint32_t m_baseClockMhz{0};

        /// False until one sample has been taken, since the counter is a rate and
        /// needs two readings to produce its first value.
        bool m_hasBaseline{false};
    };
}
