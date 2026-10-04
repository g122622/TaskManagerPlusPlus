#include "Platform/Windows/WindowsSystemProbe.h"

#include "Platform/Windows/NtdllApi.h"

#include <windows.h>

#include <psapi.h>

#include <cstdlib>
#include <cstring>
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

        /**
         * @brief Reads a REG_SZ value from the processor's registry key.
         *
         * The CPU marketing name and its rated clock live under
         * HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\0. The firmware reports
         * them there; there is no Win32 API for either, which is why Task Manager
         * reads the same key.
         *
         * @return The value, or an empty string when absent or unreadable.
         */
        [[nodiscard]] std::string _readProcessorRegistryString(wchar_t const* valueName)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                              0,
                              KEY_READ,
                              &key) != ERROR_SUCCESS)
            {
                return {};
            }

            wchar_t buffer[512]{};
            DWORD bufferBytes = sizeof(buffer) - sizeof(wchar_t);
            DWORD type = 0;
            LSTATUS const status =
                RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bufferBytes);
            RegCloseKey(key);

            if (status != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
            {
                return {};
            }

            // RegQueryValueEx does not guarantee termination, so the buffer was
            // over-allocated by one wchar_t and the last slot is forced to zero.
            buffer[(sizeof(buffer) / sizeof(buffer[0])) - 1] = L'\0';

            // The registry stores UTF-16; the application works in UTF-8 internally.
            int const required = WideCharToMultiByte(CP_UTF8, 0, buffer, -1, nullptr, 0, nullptr, nullptr);
            if (required <= 1)
            {
                return {};
            }

            std::string utf8(static_cast<size_t>(required - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buffer, -1, utf8.data(), required, nullptr, nullptr);

            // The value is padded with spaces on some firmware revisions, which would
            // otherwise show up as a trailing gap in the UI.
            while (!utf8.empty() && (utf8.back() == ' ' || utf8.back() == '\0'))
            {
                utf8.pop_back();
            }
            return utf8;
        }

        /**
         * @brief Reads a REG_DWORD value from the processor's registry key.
         *
         * @return The value, or 0 when absent or of the wrong type.
         */
        [[nodiscard]] uint32_t _readProcessorRegistryDword(wchar_t const* valueName)
        {
            HKEY key = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                              0,
                              KEY_READ,
                              &key) != ERROR_SUCCESS)
            {
                return 0;
            }

            DWORD value = 0;
            DWORD valueBytes = sizeof(value);
            DWORD type = 0;
            LSTATUS const status =
                RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &valueBytes);
            RegCloseKey(key);

            if (status != ERROR_SUCCESS || type != REG_DWORD)
            {
                return 0;
            }
            return value;
        }

        /**
         * @brief Fills in the model name and clock speeds from the registry.
         *
         * ~MHz is the rated clock the firmware reports. It is labelled "base speed" by
         * Task Manager, and is what the live speed counter is a percentage of.
         */
        void _readProcessorRegistry(SystemProcessorInfo& info)
        {
            info.modelName = _readProcessorRegistryString(L"ProcessorNameString");
            info.baseClockMhz = _readProcessorRegistryDword(L"~MHz");
        }

        /**
         * @brief Fills in the cache sizes from the processor topology.
         *
         * RelationCache reports one entry per cache, and several entries describe the
         * same level on different cores. The sizes are summed per level, which is what
         * Task Manager displays.
         */
        void _readCacheSizes(SystemProcessorInfo& info)
        {
            DWORD byteLength = 0;
            if (GetLogicalProcessorInformationEx(RelationCache, nullptr, &byteLength) == 0 &&
                GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            {
                return;
            }
            if (byteLength == 0)
            {
                return;
            }

            auto buffer = std::make_unique<std::byte[]>(byteLength);
            if (GetLogicalProcessorInformationEx(
                    RelationCache,
                    reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get()),
                    &byteLength) == 0)
            {
                return;
            }

            uint64_t l1 = 0;
            uint64_t l2 = 0;
            uint64_t l3 = 0;

            DWORD offset = 0;
            while (offset < byteLength)
            {
                auto const* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX const*>(
                    buffer.get() + offset);
                if (entry->Size == 0)
                {
                    break;
                }

                if (entry->Relationship == RelationCache)
                {
                    CACHE_RELATIONSHIP const& cache = entry->Cache;
                    // A fully associative cache reports 0xFF for Associativity; that
                    // does not affect the size, so it is not consulted.
                    switch (cache.Level)
                    {
                        case 1:
                            l1 += cache.CacheSize;
                            break;
                        case 2:
                            l2 += cache.CacheSize;
                            break;
                        case 3:
                        case 4:
                            l3 += cache.CacheSize;
                            break;
                        default:
                            break;
                    }
                }

                offset += entry->Size;
            }

            info.l1CacheBytes = static_cast<uint32_t>(l1);
            info.l2CacheBytes = static_cast<uint32_t>(l2);
            info.l3CacheBytes = l3;
        }

        /**
         * @brief Fills in the virtualisation and mitigation flags.
         *
         * Task Manager's "Virtualization: Enabled" is about whether the CPU exposes
         * hardware virtualisation extensions, so that is read from CPUID directly.
         * IsProcessorFeaturePresent(PF_VIRT_FIRMWARE_ENABLED) is not used for it: on
         * this machine it reports false while CPUID reports the feature present, and
         * the CPUID bit is the one that reflects the capability the question asks
         * about.
         *
         * A hypervisor is reported separately, because when one is running these
         * readings describe the virtual CPU rather than the physical one.
         */
        void _readVirtualizationSupport(SystemProcessorInfo& info)
        {
            info.depAvailable = IsProcessorFeaturePresent(PF_NX_ENABLED) != 0;
            info.secondLevelAddressTranslation =
                IsProcessorFeaturePresent(PF_SECOND_LEVEL_ADDRESS_TRANSLATION) != 0;

            // Vendor, to know which feature bit to consult: VMX on Intel, SVM on AMD.
            int vendorRegisters[4]{};
            __cpuid(vendorRegisters, 0);

            // The vendor string is packed into EBX, EDX, ECX in that order.
            char vendor[13]{};
            std::memcpy(vendor + 0, &vendorRegisters[1], 4); // EBX
            std::memcpy(vendor + 4, &vendorRegisters[3], 4); // EDX
            std::memcpy(vendor + 8, &vendorRegisters[2], 4); // ECX

            bool virtualizationExtensions = false;
            if (std::strcmp(vendor, "GenuineIntel") == 0)
            {
                int featureRegisters[4]{};
                __cpuid(featureRegisters, 1);
                constexpr int VMX_BIT = 1 << 5;
                virtualizationExtensions = (featureRegisters[2] & VMX_BIT) != 0;
            }
            else if (std::strcmp(vendor, "AuthenticAMD") == 0)
            {
                int extendedRegisters[4]{};
                __cpuid(extendedRegisters, 0x80000001);
                constexpr int SVM_BIT = 1 << 2;
                virtualizationExtensions = (extendedRegisters[2] & SVM_BIT) != 0;
            }

            info.virtualizationFirmwareEnabled = virtualizationExtensions;

            // A hypervisor leaves its vendor string in CPUID leaf 0x40000000, and sets
            // the hypervisor-present bit in leaf 1. Both are checked: the leaf alone can
            // hold stale values on a machine with no hypervisor.
            int featureRegisters[4]{};
            __cpuid(featureRegisters, 1);
            constexpr int HYPERVISOR_PRESENT_BIT = 1 << 31;

            int hypervisorRegisters[4]{};
            __cpuid(hypervisorRegisters, 0x40000000);
            bool const vendorNonZero =
                (hypervisorRegisters[1] != 0) || (hypervisorRegisters[2] != 0) || (hypervisorRegisters[3] != 0);

            info.hypervisorPresent = ((featureRegisters[2] & HYPERVISOR_PRESENT_BIT) != 0) && vendorNonZero;

            // A running hypervisor masks the CPU's own virtualisation bits: on this
            // machine CPUID reports no VMX and IsProcessorFeaturePresent reports no
            // firmware virtualisation, yet Hyper-V is running and systeminfo confirms
            // it. The inference below is what makes the answer correct rather than
            // merely what the CPU is willing to admit to.
            //
            // It is sound rather than a guess: a hypervisor cannot start without
            // hardware virtualisation, so its presence proves the feature is enabled.
            // This is also why Task Manager reports "Enabled" on a machine where every
            // direct query for the bit says otherwise.
            if (info.hypervisorPresent)
            {
                info.virtualizationFirmwareEnabled = true;
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

        // The page-list query is probed the same way: present on every supported system, but
        // checked rather than assumed, so its absence hides one strip instead of failing a
        // sample. It needs a full-size buffer: with a short one the query answers
        // STATUS_INFO_LENGTH_MISMATCH and the capability would be reported as absent on a
        // machine that supports it.
        if (auto const query = nt::QuerySystemInformation())
        {
            constexpr SYSTEM_INFORMATION_CLASS SystemMemoryListInformation =
                static_cast<SYSTEM_INFORMATION_CLASS>(80);

            /// Matches SYSTEM_MEMORY_LIST_INFORMATION: 22 ULONG_PTR values, 176 bytes on x64.
            struct ProbeBuffer
            {
                ULONG_PTR counters[22];
            };
            static_assert(sizeof(ProbeBuffer) == 176, "the probe buffer must match the native size");

            ProbeBuffer probe{};
            ULONG returned = 0;
            m_capabilities.hasMemoryComposition =
                query(SystemMemoryListInformation, &probe, sizeof(probe), &returned) == nt::STATUS_SUCCESS;
        }

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
        // The kernel's own accounting, which GlobalMemoryStatusEx does not report. GetPerformanceInfo
        // returns all of it in one call, so the pool sizes, system cache, commit peak and the machine's
        // handle count cost a single query between them.
        PERFORMANCE_INFORMATION performance{};
        performance.cb = sizeof(performance);

        if (GetPerformanceInfo(&performance, sizeof(performance)) != FALSE)
        {
            uint64_t const pageSize = performance.PageSize;
            auto const pages = [pageSize](SIZE_T count) { return static_cast<uint64_t>(count) * pageSize; };

            info.pagedPoolBytes = pages(performance.KernelPaged);
            info.nonPagedPoolBytes = pages(performance.KernelNonpaged);
            info.systemCacheBytes = pages(performance.SystemCache);
            info.kernelTotalBytes = pages(performance.KernelTotal);

            info.committedBytes = pages(performance.CommitTotal);
            info.commitLimitBytes = pages(performance.CommitLimit);
            info.peakCommittedBytes = pages(performance.CommitPeak);

            info.systemHandleCount = performance.HandleCount;
            info.kernelAccountingAvailable = true;
        }

        return info;
    }

    Result<SystemMemoryComposition> WindowsSystemProbe::ReadMemoryComposition() const
    {
        if (!m_capabilities.hasMemoryComposition)
        {
            return Error{ErrorCode::NotSupported,
                         "SystemMemoryListInformation is unavailable; cannot read the memory breakdown",
                         "WindowsSystemProbe::ReadMemoryComposition"};
        }

        // SystemMemoryListInformation reports the page lists as counts of pages.
        //
        // The class and its layout are not declared in winternl.h, so they are stated here.
        // The structure is SYSTEM_MEMORY_LIST_INFORMATION: five named counters, then the page
        // counts broken down by standby priority, then repurposed pages, then one trailing
        // counter. That is twenty-two ULONG_PTR values, and the query reports a required
        // length of 176 bytes on x64, which confirms the shape.
        //
        // The size matters: an earlier version of this probe passed a 32-byte buffer, the
        // query answered STATUS_INFO_LENGTH_MISMATCH, and the whole breakdown was reported as
        // unsupported on a machine that supports it perfectly well.
        constexpr SYSTEM_INFORMATION_CLASS SystemMemoryListInformation = static_cast<SYSTEM_INFORMATION_CLASS>(80);

        struct MemoryListInformation
        {
            ULONG_PTR zeroPageCount;
            ULONG_PTR freePageCount;
            ULONG_PTR modifiedPageCount;
            ULONG_PTR modifiedNoWritePageCount;
            ULONG_PTR badPageCount;

            /// Standby pages, subdivided by priority. Their sum is the cached figure.
            ULONG_PTR pageCountByPriority[8];

            /// Pages repurposed for another purpose; not part of the four segments shown.
            ULONG_PTR repurposedPagesByPriority[8];

            ULONG_PTR modifiedPageCountPageFile;
        };
        static_assert(sizeof(MemoryListInformation) == 176,
                      "SYSTEM_MEMORY_LIST_INFORMATION must be 22 ULONG_PTR values");

        MemoryListInformation info{};
        ULONG returned = 0;

        auto const query = nt::QuerySystemInformation();
        if (query == nullptr)
        {
            return Error{ErrorCode::NotSupported,
                         "NtQuerySystemInformation is unavailable",
                         "WindowsSystemProbe::ReadMemoryComposition"};
        }

        LONG const status = query(SystemMemoryListInformation, &info, sizeof(info), &returned);
        if (status != nt::STATUS_SUCCESS)
        {
            return Error{ErrorCode::NativeFailure,
                         "NtQuerySystemInformation(SystemMemoryListInformation) failed with status 0x" +
                             std::to_string(static_cast<unsigned long>(status)),
                         "WindowsSystemProbe::ReadMemoryComposition"};
        }

        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);
        uint64_t const pageSize = systemInfo.dwPageSize;
        if (pageSize == 0)
        {
            return Error{ErrorCode::NativeFailure,
                         "GetNativeSystemInfo reported a zero page size",
                         "WindowsSystemProbe::ReadMemoryComposition"};
        }

        SystemMemoryComposition composition;
        composition.pageSize = pageSize;

        // Zeroed and free pages are both immediately available, so they form one segment.
        composition.freeBytes = (static_cast<uint64_t>(info.zeroPageCount) +
                                 static_cast<uint64_t>(info.freePageCount)) * pageSize;

        composition.modifiedBytes = static_cast<uint64_t>(info.modifiedPageCount) * pageSize;

        // The standby list is the cached figure, and it is split across priority levels. Its
        // parts are summed rather than read from a single field, which is why the earlier
        // attempt to index a flat array produced a wrong count.
        uint64_t standbyPages = 0;
        for (ULONG_PTR const pages : info.pageCountByPriority)
        {
            standbyPages += static_cast<uint64_t>(pages);
        }
        composition.standbyBytes = standbyPages * pageSize;

        // In use is what remains once the other lists are accounted for. Deriving it rather
        // than reading it keeps the four segments summing to the installed total, which is what
        // the composition bar depends on: a strip whose parts do not add up shows a visible gap
        // or overrun.
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory) != 0)
        {
            uint64_t const accounted = composition.modifiedBytes + composition.standbyBytes + composition.freeBytes;
            composition.inUseBytes = (memory.ullTotalPhys > accounted) ? (memory.ullTotalPhys - accounted) : 0;
        }

        composition.available = true;
        return composition;
    }

    Result<SystemProcessorInfo> WindowsSystemProbe::ReadProcessorInfo() const
    {
        SYSTEM_INFO systemInfo{};
        GetNativeSystemInfo(&systemInfo);

        SystemProcessorInfo info;
        info.logicalProcessorCount = systemInfo.dwNumberOfProcessors;
        info.architecture = _architectureName(systemInfo.wProcessorArchitecture);

        _readProcessorRegistry(info);
        _readCacheSizes(info);

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

        _readVirtualizationSupport(info);

        // Sockets: RelationProcessorPackage reports one entry per physical package.
        DWORD packageBytes = 0;
        if (GetLogicalProcessorInformationEx(RelationProcessorPackage, nullptr, &packageBytes) == 0 &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER && packageBytes > 0)
        {
            auto buffer = std::make_unique<std::byte[]>(packageBytes);
            if (GetLogicalProcessorInformationEx(
                    RelationProcessorPackage,
                    reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get()),
                    &packageBytes) != 0)
            {
                uint32_t packages = 0;
                DWORD offset = 0;
                while (offset < packageBytes)
                {
                    auto const* entry = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX const*>(
                        buffer.get() + offset);
                    if (entry->Relationship == RelationProcessorPackage)
                    {
                        ++packages;
                    }
                    if (entry->Size == 0)
                    {
                        break;
                    }
                    offset += entry->Size;
                }
                info.socketCount = packages;
            }
        }

        // A single package is the overwhelming majority of machines and the right
        // answer when the query failed, so the field is never left at zero.
        if (info.socketCount == 0)
        {
            info.socketCount = 1;
        }

        return info;
    }

    Result<SystemTotals> WindowsSystemProbe::ReadTotals(uint32_t processCount,
                                                        uint32_t threadCount,
                                                        uint32_t handleCount) const
    {
        SystemTotals totals;
        totals.processCount = processCount;
        totals.threadCount = threadCount;
        totals.handleCount = handleCount;

        // GetTickCount64 is the time since boot in milliseconds and cannot fail, so
        // there is no error path to report for it.
        totals.uptimeSeconds = GetTickCount64() / 1000ull;
        return totals;
    }
}