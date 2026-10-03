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
    };
}
