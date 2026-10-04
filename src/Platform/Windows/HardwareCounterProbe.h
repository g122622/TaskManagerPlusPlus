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
#include <map>
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
        [[nodiscard]] Result<std::vector<SystemNetworkCounters>> ReadNetwork();

        /**
         * @brief Reads GPU utilisation and memory.
         *
         * Utilisation comes from the GPU Engine counters, which Windows publishes per engine per
         * process, so the busiest engine is what is reported. Adapter memory comes from the GPU
         * Adapter Memory counters.
         */
        [[nodiscard]] Result<SystemGpuInfo> ReadGpu();

        /**
         * @brief Reads each process's GPU utilisation, keyed by process id.
         *
         * The same counters as ReadGpu, kept per process instead of reduced to one figure. The instance
         * names carry the owning process id, so this is a second pass over data that is already being
         * collected rather than a second query.
         *
         * A process's figure is its busiest engine rather than the sum of its engines, matching how the
         * adapter's total is derived: summing would report above 100 percent for a process using several
         * engines at once, and no engine is ever more than fully busy.
         *
         * @param out Receives the per-process percentages. Cleared first, so a process that has stopped
         *        using the GPU disappears rather than keeping its last reading.
         */
        void ReadProcessGpu(std::map<uint32_t, double>& out);

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

        /// Builds the interface index cache by enumerating once. Called lazily and again after an
        /// adapter disappears, never on every sample: the enumeration is what costs hundreds of
        /// milliseconds, while polling a known index costs two.
        bool _enumerateInterfaceIndices();

        void _close() noexcept;

        /// Reads the adapter totals that do not change while the adapter is present.
        void _resolveGpuTotals() const;

        /// Opaque PDH handles, so pdh.h stays out of this header.
        void* m_gpuQuery{nullptr};
        void* m_gpuEngineCounter{nullptr};
        void* m_gpuMemoryCounter{nullptr};
        void* m_gpuSharedCounter{nullptr};

        bool m_disksAvailable{false};

        bool m_networkAvailable{false};

        /// Interface indices, enumerated once and then polled individually. Reading every interface
        /// through the bulk call took hundreds of milliseconds per sample, which made the sampling
        /// interval a fiction; polling one index at a time costs two.
        std::vector<uint32_t> m_interfaceIndices;

        /// Whether the interface list has been built. It is built once and kept: the enumeration is
        /// the expensive part and the set of adapters does not change during a session.
        bool m_interfacesEnumerated{false};

        bool m_gpuAvailable{false};

        /// Adapter totals, resolved once. Dedicated memory does not change while the adapter is
        /// present, and querying it costs as much as the utilisation does.
        mutable uint64_t m_gpuDedicatedTotal{0};
        mutable std::string m_gpuName;

        /// The driver version, resolved with the adapter totals.
        mutable std::string m_gpuDriverVersion;
        mutable bool m_gpuTotalsResolved{false};
    };
}
