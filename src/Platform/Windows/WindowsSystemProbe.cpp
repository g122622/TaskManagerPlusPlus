#include "Platform/Windows/WindowsSystemProbe.h"

#include "Platform/Windows/NtdllApi.h"

#include <windows.h>

#include <cstdlib>
#include <memory>
#include <winternl.h>

namespace tmpp::platform
{
    namespace
    {
        /**
         * @brief One entry per logical processor, as returned by
         *        SystemProcessorPerformanceInformation.
         */
        struct ProcessorPerformanceInfo
        {
            LARGE_INTEGER idleTime;
            LARGE_INTEGER kernelTime;
            LARGE_INTEGER userTime;
            LARGE_INTEGER dpcTime;
            LARGE_INTEGER interruptTime;
            ULONG interruptCount;
        };
        static_assert(sizeof(ProcessorPerformanceInfo) == 48,
                      "ProcessorPerformanceInfo must match the native 48-byte layout");

        [[nodiscard]] uint64_t _toTicks(LARGE_INTEGER value) noexcept
        {
            return static_cast<uint64_t>(value.QuadPart);
        }

        /// Per-processor buffer starts here and grows if the machine has more CPUs.
        constexpr size_t INITIAL_PROCESSOR_BYTES = 64 * sizeof(ProcessorPerformanceInfo);
        constexpr size_t MAX_PROCESSOR_BYTES = 4096 * sizeof(ProcessorPerformanceInfo);

        [[nodiscard]] std::string _architectureName(WORD architecture) noexcept
        {
            switch (architecture)
            {
                case PROCESSOR_ARCHITECTURE_AMD64:
                    return "x64";
                case PROCESSOR_ARCHITECTURE_INTEL:
                    return "x86";
                case PROCESSOR_ARCHITECTURE_ARM64:
                    return "ARM64";
                case PROCESSOR_ARCHITECTURE_ARM:
                    return "ARM";
                default:
                    return "unknown";
            }
        }
    }

    WindowsSystemProbe::WindowsSystemProbe()
    {
        // GetSystemTimes and GlobalMemoryStatusEx are in kernel32 and present on
        // every supported system; probe them rather than assuming.
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        m_capabilities.hasCpuTimes = GetSystemTimes(&idle, &kernel, &user) != 0;

        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        m_capabilities.hasMemoryInfo = GlobalMemoryStatusEx(&memory) != 0;

        m_capabilities.hasPerProcessorCpuTimes = nt::QuerySystemInformation() != nullptr;

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        m_capabilities.hasProcessorTopology = systemInfo.dwNumberOfProcessors > 0;
    }

    Result<SystemCpuTimes> WindowsSystemProbe::ReadCpuTimes() const
    {
        FILETIME idle{};
        FILETIME kernel{};
        FILETIME user{};
        if (GetSystemTimes(&idle, &kernel, &user) == 0)
        {
            return Error{ErrorCode::NativeFailure,
                         "GetSystemTimes failed with error " + std::to_string(GetLastError()),
                         "WindowsSystemProbe::ReadCpuTimes"};
        }

        // FILETIME and LARGE_INTEGER share the same two-DWORD layout, so the
        // conversion is a straight reinterpretation of the pairs.
        auto const toTicks = [](FILETIME const& ft) noexcept -> uint64_t {
            ULARGE_INTEGER value{};
            value.LowPart = ft.dwLowDateTime;
            value.HighPart = ft.dwHighDateTime;
            return value.QuadPart;
        };

        SystemCpuTimes times;
        times.idleTime = toTicks(idle);
        times.kernelTime = toTicks(kernel);
        times.userTime = toTicks(user);
        return times;
    }

    Result<std::vector<ProcessorCpuTimes>> WindowsSystemProbe::ReadPerProcessorCpuTimes() const
    {
        if (!m_capabilities.hasPerProcessorCpuTimes)
        {
            return Error{ErrorCode::NotSupported,
                         "NtQuerySystemInformation is unavailable; cannot read per-processor CPU times",
                         "WindowsSystemProbe::ReadPerProcessorCpuTimes"};
        }

        void* rawBuffer = nullptr;
        LONG status = 0;
        if (!nt::QueryWithGrowingBuffer(SystemProcessorPerformanceInformation,
                                        INITIAL_PROCESSOR_BYTES,
                                        MAX_PROCESSOR_BYTES,
                                        &rawBuffer,
                                        &status))
        {
            return Error{ErrorCode::NativeFailure,
                         "NtQuerySystemInformation(SystemProcessorPerformanceInformation) failed with status 0x" +
                             std::to_string(static_cast<unsigned long>(status)),
                         "WindowsSystemProbe::ReadPerProcessorCpuTimes"};
        }

        struct BufferGuard
        {
            void* ptr;
            ~BufferGuard() { std::free(ptr); }
        } guard{rawBuffer};

        // The API returns no length, so the count comes from the machine topology.
        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        size_t const processorCount = systemInfo.dwNumberOfProcessors;

        auto const* entries = static_cast<ProcessorPerformanceInfo const*>(rawBuffer);

        std::vector<ProcessorCpuTimes> result;
        result.reserve(processorCount);
        for (size_t i = 0; i < processorCount; ++i)
        {
            ProcessorCpuTimes times;
            times.idleTime = _toTicks(entries[i].idleTime);
            times.kernelTime = _toTicks(entries[i].kernelTime);
            times.userTime = _toTicks(entries[i].userTime);
            times.interruptTime = _toTicks(entries[i].interruptTime);
            times.dpcTime = _toTicks(entries[i].dpcTime);
            result.push_back(times);
        }

        return result;
    }

    Result<SystemMemoryInfo> WindowsSystemProbe::ReadMemoryInfo() const
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (GlobalMemoryStatusEx(&status) == 0)
        {
            return Error{ErrorCode::NativeFailure,
                         "GlobalMemoryStatusEx failed with error " + std::to_string(GetLastError()),
                         "WindowsSystemProbe::ReadMemoryInfo"};
        }

        SystemMemoryInfo info;
        info.totalPhysical = status.ullTotalPhys;
        info.availablePhysical = status.ullAvailPhys;
        info.totalPageFile = status.ullTotalPageFile;
        info.availablePageFile = status.ullAvailPageFile;
        info.totalVirtual = status.ullTotalVirtual;
        info.availableVirtual = status.ullAvailVirtual;
        info.memoryLoadPercent = status.dwMemoryLoad;
        return info;
    }

    Result<SystemProcessorInfo> WindowsSystemProbe::ReadProcessorInfo() const
    {
        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);

        SystemProcessorInfo info;
        info.logicalProcessorCount = systemInfo.dwNumberOfProcessors;
        info.architecture = _architectureName(systemInfo.wProcessorArchitecture);

        // RelationProcessorCore reports one entry per physical core, which is how
        // the logical-to-physical ratio is obtained without guessing about SMT.
        DWORD byteLength = 0;
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &byteLength) == 0 &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER && byteLength > 0)
        {
            auto buffer = std::make_unique<std::byte[]>(byteLength);
            if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                                                 reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get()),
                                                 &byteLength) != 0)
            {
                uint32_t cores = 0;
                DWORD offset = 0;
                while (offset < byteLength)
                {
                    auto const* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX const*>(
                        buffer.get() + offset);
                    if (entry->Relationship == RelationProcessorCore)
                    {
                        ++cores;
                    }
                    if (entry->Size == 0)
                    {
                        break;
                    }
                    offset += entry->Size;
                }
                info.physicalCoreCount = cores;
            }
        }

        // Fall back to the logical count when the topology query is unavailable so
        // the field is never reported as a misleading zero.
        if (info.physicalCoreCount == 0)
        {
            info.physicalCoreCount = info.logicalProcessorCount;
        }

        return info;
    }
}