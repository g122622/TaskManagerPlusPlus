// Platform-neutral system-wide data types.
//
// The Domain layer depends on this header rather than on the Windows probe
// header, keeping "Domain depends only on Platform interfaces" true.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tmpp::platform
{
    /**
     * @brief System-wide cumulative CPU time.
     *
     * IMPORTANT: on Windows, kernel time *includes* idle time. Consumers must
     * subtract idle to obtain real busy time; that subtraction belongs to the
     * Domain layer. The raw values are preserved here so the maths stays
     * testable and the readings stay inspectable.
     */
    struct SystemCpuTimes
    {
        uint64_t idleTime{0};   ///< 100 ns intervals.
        uint64_t kernelTime{0}; ///< 100 ns intervals; INCLUDES idleTime.
        uint64_t userTime{0};   ///< 100 ns intervals.

        /// Kernel time excluding idle, i.e. real kernel work. Saturating.
        [[nodiscard]] uint64_t KernelExcludingIdle() const noexcept
        {
            return (kernelTime > idleTime) ? (kernelTime - idleTime) : 0;
        }

        /// Total busy time. Saturating.
        [[nodiscard]] uint64_t Busy() const noexcept { return KernelExcludingIdle() + userTime; }

        /// Total elapsed CPU time across all processors, including idle.
        [[nodiscard]] uint64_t Total() const noexcept { return kernelTime + userTime; }
    };

    /**
     * @brief Per-logical-processor cumulative CPU time.
     */
    struct ProcessorCpuTimes
    {
        uint64_t idleTime{0};
        uint64_t kernelTime{0};
        uint64_t userTime{0};
        uint64_t interruptTime{0};
        uint64_t dpcTime{0};
    };

    /**
     * @brief Physical and virtual memory state, in bytes.
     */
    struct SystemMemoryInfo
    {
        uint64_t totalPhysical{0};
        uint64_t availablePhysical{0};
        uint64_t totalPageFile{0};
        uint64_t availablePageFile{0};
        uint64_t totalVirtual{0};
        uint64_t availableVirtual{0};
        uint32_t memoryLoadPercent{0};
    };

    /**
     * @brief Static description of the machine's processors.
     */
    struct SystemProcessorInfo
    {
        uint32_t logicalProcessorCount{0};
        uint32_t physicalCoreCount{0};
        std::string architecture; ///< e.g. "x64", "ARM64".

        /// Marketing name, e.g. "Intel(R) Core(TM) i7-14700KF". Empty when the
        /// registry value is absent or unreadable.
        std::string modelName;

        /// Nominal (rated) clock in MHz, from the registry. Zero when unknown.
        uint32_t baseClockMhz{0};

        /// Maximum clock in MHz as reported by the firmware, zero when unknown.
        uint32_t maxClockMhz{0};

        /// Number of physical processor packages (sockets).
        uint32_t socketCount{0};

        /// Last-level cache size in bytes; zero when the topology query failed.
        uint64_t l3CacheBytes{0};

        uint32_t l2CacheBytes{0};
        uint32_t l1CacheBytes{0};

        /// True when a hypervisor is present, whether or not this process can see
        /// through it. "Enabled" in Task Manager means firmware virtualisation
        /// extensions are on, which is a separate question and is reported
        /// separately below.
        bool hypervisorPresent{false};

        /// Whether the firmware reports virtualisation extensions as enabled.
        bool virtualizationFirmwareEnabled{false};

        /// Whether second-level address translation is available.
        bool secondLevelAddressTranslation{false};

        /// Whether DEP is available.
        bool depAvailable{false};
    };

    /**
     * @brief Live clock speed of the processor.
     *
     * Separate from SystemProcessorInfo because it changes every sample while the
     * topology does not, and because its source (a performance counter) can be
     * unavailable where the registry values are not.
     */
    struct SystemProcessorSpeed
    {
        /// Current effective clock in MHz, zero when it could not be read.
        uint32_t currentMhz{0};

        /// True when currentMhz came from a counter rather than being derived.
        bool available{false};
    };

    /**
     * @brief Rolling system-wide totals that are not per-process.
     *
     * The counts are read from the same bulk process snapshot the process list
     * uses, so they cost nothing extra; the uptime comes from the tick count.
     */
    struct SystemTotals
    {
        uint32_t processCount{0};
        uint32_t threadCount{0};
        uint32_t handleCount{0};

        /// Seconds since the system started.
        uint64_t uptimeSeconds{0};
    };

    /**
     * @brief How physical memory is currently distributed, in bytes.
     *
     * Windows does not expose this through GlobalMemoryStatusEx, which reports only total and
     * available. The breakdown comes from SystemMemoryListInformation, which reports the page
     * lists by type; the values here are those page counts converted to bytes.
     *
     * The four categories are mutually exclusive and together account for all physical memory,
     * which is what lets the composition bar be drawn as one continuous strip.
     */
    struct SystemMemoryComposition
    {
        /// Pages backing live allocations: process working sets, the kernel, drivers.
        uint64_t inUseBytes{0};

        /// Pages written but not yet flushed to their backing store.
        uint64_t modifiedBytes{0};

        /// Pages holding cached file data, reusable on demand. This is the standby list.
        uint64_t standbyBytes{0};

        /// Pages on the free list, immediately available.
        uint64_t freeBytes{0};

        /// Page size used for the conversion, for diagnostics.
        uint64_t pageSize{0};

        /// True when the breakdown came from the page lists rather than being derived.
        bool available{false};

        /// Total accounted for, which should equal installed physical memory.
        [[nodiscard]] uint64_t Total() const noexcept
        {
            return inUseBytes + modifiedBytes + standbyBytes + freeBytes;
        }
    };

    /**
     * @brief Which system metrics this machine can provide.
     */
    struct SystemCapabilities
    {
        bool hasCpuTimes{false};
        bool hasPerProcessorCpuTimes{false};
        bool hasMemoryInfo{false};
        bool hasProcessorTopology{false};

        /// True when a processor performance counter is queryable, which is what
        /// makes the live clock speed available.
        bool hasProcessorPerformance{false};

        /// True when the memory page lists are queryable, which is what makes the
        /// composition breakdown available.
        bool hasMemoryComposition{false};
    };
}
