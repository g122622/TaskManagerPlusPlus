// Raw process counters read from Windows.
//
// Every field is a cumulative counter or a direct reading. No rates,
// percentages or deltas are computed here: that is the Domain layer's job
// (see docs/ARCHITECTURE.md, "Hard dependency rules").
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Platform/Result.h"

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
        uint64_t createTime{0}; ///< 100 ns intervals since 1601-01-01 (FILETIME).

        [[nodiscard]] bool operator==(ProcessIdentity const& other) const noexcept
        {
            return pid == other.pid && createTime == other.createTime;
        }
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
        uint64_t readTransferCount{0};
        uint64_t writeTransferCount{0};
        uint64_t otherTransferCount{0};
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
     * @brief One process as reported by a single bulk snapshot.
     */
    struct ProcessInfo
    {
        ProcessIdentity identity;
        uint32_t parentPid{0};
        std::string imageName; ///< UTF-8, base name only (from the snapshot).
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
        uint64_t capturedAt{0}; ///< QPC ticks when the snapshot was taken.
    };

    /**
     * @brief Which optional process metrics this system can provide.
     *
     * A missing capability is not an error: the UI hides the affected column
     * instead of showing a fabricated zero.
     */
    struct ProcessCapabilities
    {
        bool hasBulkEnumeration{false}; ///< NtQuerySystemInformation is available.
        bool hasIoCounters{false};
        bool hasCpuTimes{false};
        bool hasMemoryCounters{false};
        bool hasThreadAndHandleCounts{false};
    };

    /**
     * @brief Reads process counters from Windows.
     */
    class WindowsProcessProbe
    {
    public:
        WindowsProcessProbe();

        /**
         * @brief Enumerates all processes in a single bulk snapshot.
         *
         * Uses one NtQuerySystemInformation(SystemProcessInformation) call for the
         * whole system. No per-process handle is opened, which is what keeps the
         * cost roughly independent of the process count.
         */
        [[nodiscard]] Result<ProcessSnapshot> Enumerate() const;

        [[nodiscard]] ProcessCapabilities Capabilities() const noexcept { return m_capabilities; }

    private:
        ProcessCapabilities m_capabilities;
    };
}
