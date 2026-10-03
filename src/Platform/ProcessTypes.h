// Platform-neutral process data types.
//
// These describe what a probe returns, without naming any Windows API. The
// Domain layer depends on this header rather than on the Windows probe header,
// which keeps "Domain depends only on Platform interfaces" true and would let a
// second platform back end be added without touching the Domain layer.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tmpp::platform
{
    /**
     * @brief Identity of a process that survives PID reuse.
     *
     * Windows recycles PIDs. Keying state on the PID alone would merge two
     * unrelated processes that happened to share a number, so the process
     * creation time is part of the identity.
     */
    struct ProcessIdentity
    {
        uint32_t pid{0};
        uint64_t createTime{0}; ///< 100 ns intervals since 1601-01-01.

        [[nodiscard]] bool operator==(ProcessIdentity const& other) const noexcept
        {
            return pid == other.pid && createTime == other.createTime;
        }

        [[nodiscard]] bool operator!=(ProcessIdentity const& other) const noexcept { return !(*this == other); }
    };

    /**
     * @brief Cumulative CPU time consumed by a process.
     */
    struct ProcessCpuTimes
    {
        uint64_t kernelTime{0}; ///< 100 ns intervals.
        uint64_t userTime{0};   ///< 100 ns intervals.

        [[nodiscard]] uint64_t Total() const noexcept { return kernelTime + userTime; }
    };

    /**
     * @brief Cumulative disk I/O counters for a process.
     */
    struct ProcessIoCounters
    {
        uint64_t readTransferCount{0};  ///< Bytes.
        uint64_t writeTransferCount{0}; ///< Bytes.
        uint64_t otherTransferCount{0}; ///< Bytes.
        uint64_t readOperationCount{0};
        uint64_t writeOperationCount{0};
        uint64_t otherOperationCount{0};
    };

    /**
     * @brief Memory counters for a process, in bytes.
     */
    struct ProcessMemoryCounters
    {
        uint64_t workingSetSize{0};
        uint64_t peakWorkingSetSize{0};
        uint64_t privatePageCount{0};
        uint64_t virtualSize{0};
        uint64_t peakVirtualSize{0};
        uint64_t pagefileUsage{0};
        uint64_t peakPagefileUsage{0};
        uint32_t pageFaultCount{0};
    };

    /**
     * @brief One process as reported by a single snapshot.
     */
    struct ProcessInfo
    {
        ProcessIdentity identity;
        uint32_t parentPid{0};

        /// Raw image name from the platform. Empty for a process with no
        /// executable image (the Windows idle process); presenting that as a name
        /// is the consumer's decision, not the probe's.
        std::string imageName;

        ProcessCpuTimes cpu;
        ProcessMemoryCounters memory;
        ProcessIoCounters io;

        uint32_t threadCount{0};
        uint32_t handleCount{0};
        uint32_t sessionId{0};
        int32_t basePriority{0};
    };

    /**
     * @brief A complete process snapshot taken at one instant.
     */
    struct ProcessSnapshot
    {
        std::vector<ProcessInfo> processes;

        /// Monotonic timestamp of the reading. Opaque; only differences are
        /// meaningful.
        uint64_t capturedAt{0};
    };

    /**
     * @brief Which optional process metrics this system can provide.
     *
     * A missing capability is not an error: the UI hides the affected column
     * instead of showing a fabricated zero.
     */
    struct ProcessCapabilities
    {
        bool hasBulkEnumeration{false};
        bool hasIoCounters{false};
        bool hasCpuTimes{false};
        bool hasMemoryCounters{false};
        bool hasThreadAndHandleCounts{false};
    };
}
