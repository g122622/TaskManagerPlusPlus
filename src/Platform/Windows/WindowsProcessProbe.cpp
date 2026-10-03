#include "WindowsProcessProbe.h"

#include "Platform/Windows/NtdllApi.h"
#include "Platform/Windows/WindowsString.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>
#include <winternl.h>

namespace tmpp::platform
{
    namespace
    {
        // --------------------------------------------------------------------
        // SYSTEM_PROCESS_INFORMATION
        //
        // winternl.h declares a truncated variant of this structure, hiding most
        // fields behind Reserved blocks. The complete layout below has been a
        // stable ABI since Windows XP and is what one bulk snapshot gives us:
        // CPU times, memory, I/O, handle/thread counts and names for every
        // process, with no per-process handle.
        // --------------------------------------------------------------------
        struct SystemProcessInfo
        {
            ULONG nextEntryOffset;
            ULONG numberOfThreads;
            LARGE_INTEGER workingSetPrivateSize;
            ULONG hardFaultCount;
            ULONG numberOfThreadsHighWatermark;
            ULONGLONG cycleTime;
            LARGE_INTEGER createTime;
            LARGE_INTEGER userTime;
            LARGE_INTEGER kernelTime;
            UNICODE_STRING imageName;
            LONG basePriority;
            HANDLE uniqueProcessId;
            HANDLE inheritedFromUniqueProcessId;
            ULONG handleCount;
            ULONG sessionId;
            ULONG_PTR uniqueProcessKey;
            SIZE_T peakVirtualSize;
            SIZE_T virtualSize;
            ULONG pageFaultCount;
            SIZE_T peakWorkingSetSize;
            SIZE_T workingSetSize;
            SIZE_T quotaPeakPagedPoolUsage;
            SIZE_T quotaPagedPoolUsage;
            SIZE_T quotaPeakNonPagedPoolUsage;
            SIZE_T quotaNonPagedPoolUsage;
            SIZE_T pagefileUsage;
            SIZE_T peakPagefileUsage;
            SIZE_T privatePageCount;
            LARGE_INTEGER readOperationCount;
            LARGE_INTEGER writeOperationCount;
            LARGE_INTEGER otherOperationCount;
            LARGE_INTEGER readTransferCount;
            LARGE_INTEGER writeTransferCount;
            LARGE_INTEGER otherTransferCount;
        };

        // Compile-time ABI validation. winternl.h hides most of the payload behind
        // Reserved blocks, so only the fields the SDK actually names can be checked
        // by name; the hidden ones are verified through the block offsets below.
        static_assert(sizeof(SystemProcessInfo) == sizeof(SYSTEM_PROCESS_INFORMATION),
                      "SystemProcessInfo no longer matches the SDK layout");
        static_assert(offsetof(SystemProcessInfo, nextEntryOffset) == offsetof(SYSTEM_PROCESS_INFORMATION, NextEntryOffset));
        static_assert(offsetof(SystemProcessInfo, numberOfThreads) == offsetof(SYSTEM_PROCESS_INFORMATION, NumberOfThreads));
        static_assert(offsetof(SystemProcessInfo, imageName) == offsetof(SYSTEM_PROCESS_INFORMATION, ImageName));
        static_assert(offsetof(SystemProcessInfo, basePriority) == offsetof(SYSTEM_PROCESS_INFORMATION, BasePriority));
        static_assert(offsetof(SystemProcessInfo, uniqueProcessId) == offsetof(SYSTEM_PROCESS_INFORMATION, UniqueProcessId));
        static_assert(offsetof(SystemProcessInfo, handleCount) == offsetof(SYSTEM_PROCESS_INFORMATION, HandleCount));
        static_assert(offsetof(SystemProcessInfo, sessionId) == offsetof(SYSTEM_PROCESS_INFORMATION, SessionId));
        static_assert(offsetof(SystemProcessInfo, peakVirtualSize) == offsetof(SYSTEM_PROCESS_INFORMATION, PeakVirtualSize));
        static_assert(offsetof(SystemProcessInfo, virtualSize) == offsetof(SYSTEM_PROCESS_INFORMATION, VirtualSize));
        static_assert(offsetof(SystemProcessInfo, peakWorkingSetSize) == offsetof(SYSTEM_PROCESS_INFORMATION, PeakWorkingSetSize));
        static_assert(offsetof(SystemProcessInfo, workingSetSize) == offsetof(SYSTEM_PROCESS_INFORMATION, WorkingSetSize));
        static_assert(offsetof(SystemProcessInfo, quotaPagedPoolUsage) == offsetof(SYSTEM_PROCESS_INFORMATION, QuotaPagedPoolUsage));
        static_assert(offsetof(SystemProcessInfo, quotaNonPagedPoolUsage) == offsetof(SYSTEM_PROCESS_INFORMATION, QuotaNonPagedPoolUsage));
        static_assert(offsetof(SystemProcessInfo, pagefileUsage) == offsetof(SYSTEM_PROCESS_INFORMATION, PagefileUsage));
        static_assert(offsetof(SystemProcessInfo, peakPagefileUsage) == offsetof(SYSTEM_PROCESS_INFORMATION, PeakPagefileUsage));
        static_assert(offsetof(SystemProcessInfo, privatePageCount) == offsetof(SYSTEM_PROCESS_INFORMATION, PrivatePageCount));

        // Fields the SDK hides behind Reserved blocks. Their offsets must be
        // verified against the block they live in, otherwise a SDK layout change
        // would silently produce garbage counters.
        //
        //   Reserved1[48] holds, in order:
        //     WorkingSetPrivateSize, HardFaultCount, NumberOfThreadsHighWatermark,
        //     CycleTime, CreateTime, UserTime, KernelTime
        //   Reserved2 is InheritedFromUniqueProcessId.
        //   Reserved7[6] holds the six I/O counters.
        static_assert(offsetof(SystemProcessInfo, workingSetPrivateSize) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1));
        static_assert(offsetof(SystemProcessInfo, createTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 24);
        static_assert(offsetof(SystemProcessInfo, userTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 32);
        static_assert(offsetof(SystemProcessInfo, kernelTime) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved1) + 40);
        static_assert(offsetof(SystemProcessInfo, inheritedFromUniqueProcessId) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved2));
        static_assert(offsetof(SystemProcessInfo, readOperationCount) == offsetof(SYSTEM_PROCESS_INFORMATION, Reserved7));

        /// Initial snapshot buffer. Grown by the query helper when the system has
        /// more processes than this fits.
        constexpr size_t INITIAL_SNAPSHOT_BYTES = 512 * 1024;

        /// Ceiling on snapshot growth. 256 MiB is far above any real system while
        /// still bounding memory use if the API misreports the required size.
        constexpr size_t MAX_SNAPSHOT_BYTES = 256ull * 1024 * 1024;

        /// Reserved capacity, matching the referenced implementation's tuning.
        constexpr size_t ESTIMATED_PROCESS_COUNT = 512;

        [[nodiscard]] uint64_t _toTicks(LARGE_INTEGER value) noexcept
        {
            return static_cast<uint64_t>(value.QuadPart);
        }

        /**
         * @brief Extracts the base file name from a snapshot image name.
         *
         * The snapshot reports a base name already on modern Windows, but older
         * builds and some drivers include a path, so this trims defensively.
         */
        [[nodiscard]] std::string _baseNameFromSnapshot(UNICODE_STRING const& name)
        {
            if (name.Buffer == nullptr || name.Length == 0)
            {
                return {};
            }

            auto const codeUnits = static_cast<size_t>(name.Length) / sizeof(wchar_t);
            std::string const full = ToUtf8(name.Buffer, codeUnits);

            auto const slash = full.find_last_of("\\/");
            return (slash == std::string::npos) ? full : full.substr(slash + 1);
        }
    }

    WindowsProcessProbe::WindowsProcessProbe()
    {
        m_capabilities.hasBulkEnumeration = nt::QuerySystemInformation() != nullptr;
        // The bulk snapshot carries every field below in one call, so all of these
        // capabilities follow the availability of the query itself.
        m_capabilities.hasCpuTimes = m_capabilities.hasBulkEnumeration;
        m_capabilities.hasMemoryCounters = m_capabilities.hasBulkEnumeration;
        m_capabilities.hasIoCounters = m_capabilities.hasBulkEnumeration;
        m_capabilities.hasThreadAndHandleCounts = m_capabilities.hasBulkEnumeration;
    }

    Result<ProcessSnapshot> WindowsProcessProbe::Enumerate() const
    {
        if (!m_capabilities.hasBulkEnumeration)
        {
            return Error{ErrorCode::NotSupported,
                         "NtQuerySystemInformation is unavailable; cannot enumerate processes",
                         "WindowsProcessProbe::Enumerate"};
        }

        void* rawBuffer = nullptr;
        LONG status = 0;
        if (!nt::QueryWithGrowingBuffer(
                SystemProcessInformation, INITIAL_SNAPSHOT_BYTES, MAX_SNAPSHOT_BYTES, &rawBuffer, &status))
        {
            return Error{ErrorCode::NativeFailure,
                         "NtQuerySystemInformation(SystemProcessInformation) failed with status 0x" +
                             std::to_string(static_cast<unsigned long>(status)),
                         "WindowsProcessProbe::Enumerate"};
        }

        // Own the raw buffer for the rest of the function.
        struct BufferGuard
        {
            void* ptr;
            ~BufferGuard() { std::free(ptr); }
        } guard{rawBuffer};

        ProcessSnapshot snapshot;
        snapshot.processes.reserve(ESTIMATED_PROCESS_COUNT);
        LARGE_INTEGER qpc{};
        QueryPerformanceCounter(&qpc);
        snapshot.capturedAt = static_cast<uint64_t>(qpc.QuadPart);

        auto const* entry = static_cast<SystemProcessInfo const*>(rawBuffer);
        for (;;)
        {
            ProcessInfo info;
            info.identity.pid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry->uniqueProcessId));
            info.identity.createTime = _toTicks(entry->createTime);
            info.parentPid = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(entry->inheritedFromUniqueProcessId));
            info.imageName = _baseNameFromSnapshot(entry->imageName);

            info.cpu.kernelTime = _toTicks(entry->kernelTime);
            info.cpu.userTime = _toTicks(entry->userTime);

            info.memory.workingSetSize = entry->workingSetSize;
            info.memory.peakWorkingSetSize = entry->peakWorkingSetSize;
            info.memory.privatePageCount = entry->privatePageCount;
            info.memory.virtualSize = entry->virtualSize;
            info.memory.peakVirtualSize = entry->peakVirtualSize;
            info.memory.pagefileUsage = entry->pagefileUsage;
            info.memory.peakPagefileUsage = entry->peakPagefileUsage;
            info.memory.pageFaultCount = entry->pageFaultCount;

            info.io.readTransferCount = _toTicks(entry->readTransferCount);
            info.io.writeTransferCount = _toTicks(entry->writeTransferCount);
            info.io.otherTransferCount = _toTicks(entry->otherTransferCount);
            info.io.readOperationCount = _toTicks(entry->readOperationCount);
            info.io.writeOperationCount = _toTicks(entry->writeOperationCount);
            info.io.otherOperationCount = _toTicks(entry->otherOperationCount);

            info.threadCount = entry->numberOfThreads;
            info.handleCount = entry->handleCount;
            info.sessionId = entry->sessionId;
            info.basePriority = entry->basePriority;

            snapshot.processes.push_back(std::move(info));

            if (entry->nextEntryOffset == 0)
            {
                break;
            }
            entry = reinterpret_cast<SystemProcessInfo const*>(reinterpret_cast<char const*>(entry) + entry->nextEntryOffset);
        }

        return snapshot;
    }
}