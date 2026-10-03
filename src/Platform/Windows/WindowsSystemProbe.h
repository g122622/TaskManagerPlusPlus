// System-wide counters on Windows.
//
// Only the Windows implementation lives here; the data types it produces are in
// Platform/SystemTypes.h so the Domain layer never includes this header.
#pragma once

#include <vector>

#include "Platform/Result.h"
#include "Platform/SystemTypes.h"

namespace tmpp::platform
{
    /**
     * @brief Reads system-wide counters from Windows.
     */
    class WindowsSystemProbe
    {
    public:
        WindowsSystemProbe();

        /**
         * @brief Reads cumulative system-wide CPU times.
         *
         * GetSystemTimes is the cheapest reliable source for overall CPU load and
         * needs no elevation. Note that its kernel time includes idle.
         */
        [[nodiscard]] Result<SystemCpuTimes> ReadCpuTimes() const;

        /**
         * @brief Reads per-logical-processor cumulative CPU times.
         *
         * Uses NtQuerySystemInformation(SystemProcessorPerformanceInformation).
         */
        [[nodiscard]] Result<std::vector<ProcessorCpuTimes>> ReadPerProcessorCpuTimes() const;

        /**
         * @brief Reads physical and virtual memory state.
         */
        [[nodiscard]] Result<SystemMemoryInfo> ReadMemoryInfo() const;

        /**
         * @brief Reads how physical memory is currently distributed.
         *
         * Separate from ReadMemoryInfo because it needs a different, native API and can be
         * unavailable on its own; the composition bar is simply not drawn when it is.
         */
        [[nodiscard]] Result<SystemMemoryComposition> ReadMemoryComposition() const;

        /**
         * @brief Reads the processor topology.
         */
        [[nodiscard]] Result<SystemProcessorInfo> ReadProcessorInfo() const;

        /**
         * @brief Reads rolling system-wide totals.
         *
         * @param processCount Processes in the latest snapshot.
         * @param threadCount Threads across those processes.
         * @param handleCount Handles across those processes.
         *
         * The counts are passed in rather than enumerated again: they are already
         * known from the bulk process snapshot, and re-walking the process list here
         * would double the cost of every sample.
         */
        [[nodiscard]] Result<SystemTotals> ReadTotals(uint32_t processCount,
                                                      uint32_t threadCount,
                                                      uint32_t handleCount) const;

        [[nodiscard]] SystemCapabilities Capabilities() const noexcept { return m_capabilities; }

    private:
        SystemCapabilities m_capabilities;
    };
}
