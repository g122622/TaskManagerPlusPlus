// Disk, network and GPU counters on Windows.
//
// Separate from WindowsSystemProbe, which reads CPU and memory: those come from native calls that
// return a struct, while these come from performance counters and the IP Helper API, and the
// bookkeeping is entirely different. Keeping them together would have made one file that does two
// unrelated things.
//
// All three are read without elevation. That is a deliberate constraint: the original shows these
// figures to an ordinary user, and a probe that needed an administrator would leave the disk,
// network and GPU charts blank for almost everyone.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Platform/Result.h"
#include "Platform/SystemTypes.h"

namespace tmpp::platform
{
    /**
     * @brief Reads disk, network and GPU counters from Windows.
     *
     * The performance-counter queries are opened once and reused, because opening a query
     * enumerates the counter set and costs far more than collecting from it.
     */
    class HardwareCounterProbe
    {
    public:
        HardwareCounterProbe();
        ~HardwareCounterProbe();

        // Not copyable: it owns query handles and closes them on destruction.
        HardwareCounterProbe(HardwareCounterProbe const&) = delete;
        HardwareCounterProbe& operator=(HardwareCounterProbe const&) = delete;

        /**
         * @brief Reads cumulative byte and operation counters for every physical disk.
         *
         * One entry per physical device, not per volume: the drive letter in a name is a mounting
         * detail, and a device with two partitions has one set of counters covering both.
         *
         * The figures are cumulative because the rate is derived in the Domain layer by differencing
         * two samples. That is what makes the first sample honest: a rate cannot be computed from one
         * reading, and inventing a zero would claim the disk was idle.
         */
        [[nodiscard]] Result<std::vector<SystemDiskCounters>> ReadDisks();

        /**
         * @brief Reads cumulative counters for every network interface that can carry user traffic.
         *
         * Comes from the IP Helper API rather than a performance counter because it reports
         * cumulative 64-bit octet counts directly, along with the link speed and the operational
         * state, in one call.
         */
        [[nodiscard]] Result<std::vector<SystemNetworkCounters>> ReadNetwork() const;

        /**
         * @brief Reads GPU utilisation and memory.
         *
         * Utilisation comes from the GPU Engine counters, which Windows publishes per engine per
         * process, so the busiest engine is what is reported. Adapter memory comes from the GPU
         * Adapter Memory counters.
         */
        [[nodiscard]] Result<SystemGpuInfo> ReadGpu();

        /// True when at least one disk's counters were readable.
        [[nodiscard]] bool DisksAvailable() const noexcept { return m_disksAvailable; }

        /// True when the network counters were readable.
        [[nodiscard]] bool NetworkAvailable() const noexcept { return m_networkAvailable; }

        /// True when the GPU counters were readable.
        [[nodiscard]] bool GpuAvailable() const noexcept { return m_gpuAvailable; }

    private:
        /// Opens the GPU counter query. The disk counters need no persistent state: each read opens
        /// and closes a handle per device, which is cheap and avoids holding handles to hardware that
        /// may be removed.
        void _openGpu();

        void _close() noexcept;

        /// Reads the adapter totals that do not change while the adapter is present.
        void _resolveGpuTotals() const;

        /// Opaque PDH handles, so pdh.h stays out of this header.
        void* m_gpuQuery{nullptr};
        void* m_gpuEngineCounter{nullptr};
        void* m_gpuMemoryCounter{nullptr};

        bool m_disksAvailable{false};

        /// Cached availability. Mutable because ReadNetwork is const but records what it observed, so
        /// that the capability can be reported without reading the table again.
        mutable bool m_networkAvailable{false};

        bool m_gpuAvailable{false};

        /// Adapter totals, resolved once. Dedicated memory does not change while the adapter is
        /// present, and querying it costs as much as the utilisation does.
        mutable uint64_t m_gpuDedicatedTotal{0};
        mutable std::string m_gpuName;
        mutable bool m_gpuTotalsResolved{false};
    };
}
