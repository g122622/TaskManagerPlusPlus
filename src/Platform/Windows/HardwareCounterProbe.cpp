#include "Platform/Windows/HardwareCounterProbe.h"

// The include order here is required, not stylistic. netioapi.h documents that it must be reached
// through iphlpapi.h after the Winsock headers, and winsock2.h must precede windows.h or the older
// winsock.h is pulled in instead and the two sets of definitions collide.
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>

#include <iphlpapi.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winioctl.h>

#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "dxgi.lib")

namespace tmpp::platform
{
    namespace
    {
        /// The GPU counters this probe reads.
        ///
        /// The English names are required: PDH counter paths are localised on a non-English system.
        /// A localisation-aware lookup would need PdhLookupPerfNameByIndex with the index of each
        /// name, which is a larger change and is noted in docs/METRICS.md.
        ///
        /// The memory counter is GPU Adapter Memory rather than GPU Process Memory. The per-process
        /// counter reports committed reservations rather than resident memory: summing it on this
        /// machine gave 112 GB against a 4 GB card, a figure that cannot be shown as a fraction of
        /// anything. The adapter counter reports what is resident.
        constexpr wchar_t const* GPU_ENGINE_COUNTER = L"\\GPU Engine(*)\\Utilization Percentage";
        constexpr wchar_t const* GPU_MEMORY_COUNTER = L"\\GPU Adapter Memory(*)\\Dedicated Usage";

        /// The adapter's shared system memory. Read from the same counter set as the dedicated
        /// figure, so it costs one more counter on the query rather than a second query.
        constexpr wchar_t const* GPU_SHARED_COUNTER = L"\\GPU Adapter Memory(*)\\Shared Usage";

        /// The counter-set instance that aggregates every device. It must be skipped: including it
        /// would double every figure, since it is the sum of the rows already being reported.
        constexpr wchar_t const* AGGREGATE_INSTANCE = L"_Total";

        /// Buffer size for a counter array query, grown on PDH_MORE_DATA.
        constexpr DWORD INITIAL_BUFFER_BYTES = 16 * 1024;

        [[nodiscard]] std::string _pdhError(PDH_STATUS status)
        {
            char buffer[16]{};
            std::snprintf(buffer, sizeof(buffer), "%08lX", static_cast<unsigned long>(status));
            return std::string{"PDH status 0x"} + buffer;
        }

        [[nodiscard]] std::string _toNarrow(wchar_t const* text)
        {
            if (text == nullptr || text[0] == L'\0')
            {
                return {};
            }
            int const length = static_cast<int>(wcslen(text));
            int const needed = WideCharToMultiByte(CP_UTF8, 0, text, length, nullptr, 0, nullptr, nullptr);
            if (needed <= 0)
            {
                return {};
            }
            std::string narrow(static_cast<size_t>(needed), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, length, narrow.data(), needed, nullptr, nullptr);
            return narrow;
        }

        /**
         * @brief Reads a signed counter value, treating a negative one as "no data".
         *
         * DISK_PERFORMANCE's byte and time fields come from a signed LARGE_INTEGER. A virtualised or
         * passthrough disk driver can report a negative value, and casting that straight to an
         * unsigned type would wrap to near the maximum, producing a reading of several exabytes on
         * the next differencing step. Treating the sign as "no data" is the only honest reading.
         */
        [[nodiscard]] uint64_t _clampNonNegative(int64_t value) noexcept
        {
            return (value < 0) ? 0ull : static_cast<uint64_t>(value);
        }

        /**
         * @brief Extracts the leading device index from a PDH instance name.
         *
         * PDH names a physical disk "<index> <drive letters>", so "0 C:" or "1 D: E:" for a device
         * backing several volumes. The index identifies the device and is what opens the right
         * handle; the letters are what the user recognises.
         */
        [[nodiscard]] std::optional<uint32_t> _parseDeviceIndex(std::wstring const& instanceName)
        {
            size_t const space = instanceName.find(L' ');
            std::wstring const indexPart = (space == std::wstring::npos) ? instanceName : instanceName.substr(0, space);
            if (indexPart.empty())
            {
                return std::nullopt;
            }

            uint32_t index = 0;
            for (wchar_t const ch : indexPart)
            {
                if (ch < L'0' || ch > L'9')
                {
                    return std::nullopt;
                }
                index = (index * 10u) + static_cast<uint32_t>(ch - L'0');
            }
            return index;
        }

        /**
         * @brief One collected counter value belonging to a named instance.
         */
        struct InstanceValue
        {
            std::string instance;
            double value{0.0};
        };

        /**
         * @brief Reads every instance of a counter, keeping the instance name with each value.
         *
         * PdhGetFormattedCounterArray returns the items in one buffer whose strings are packed after
         * the array, so the count and the per-item names both have to be walked. Doing that by hand
         * is what makes the instance names available, and the names are how a GPU's engine instances
         * are grouped back together.
         */
        [[nodiscard]] bool _readCounterArray(PDH_HCOUNTER counter, std::vector<InstanceValue>& out)
        {
            if (counter == nullptr)
            {
                return false;
            }

            // The API takes a pointer to an item array rather than a byte buffer, so the storage is
            // typed and the required size is negotiated in bytes through a cast.
            DWORD bufferSize = INITIAL_BUFFER_BYTES;
            DWORD itemCount = 0;
            std::vector<PDH_FMT_COUNTERVALUE_ITEM_W> items(bufferSize / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);

            PDH_STATUS status = PdhGetFormattedCounterArrayW(counter,
                                                            PDH_FMT_DOUBLE,
                                                            &bufferSize,
                                                            &itemCount,
                                                            items.data());

            // The first call reports the size it needs. A couple of retries covers the case where the
            // instance set grew between the two calls.
            for (int attempt = 0; status == static_cast<PDH_STATUS>(PDH_MORE_DATA) && attempt < 4; ++attempt)
            {
                items.resize(bufferSize / sizeof(PDH_FMT_COUNTERVALUE_ITEM_W) + 1);
                status = PdhGetFormattedCounterArrayW(counter,
                                                      PDH_FMT_DOUBLE,
                                                      &bufferSize,
                                                      &itemCount,
                                                      items.data());
            }

            if (status != ERROR_SUCCESS || itemCount == 0)
            {
                return false;
            }
            for (DWORD i = 0; i < itemCount; ++i)
            {
                if (items[i].szName == nullptr)
                {
                    continue;
                }

                // A counter can report that it has no valid data even when the read succeeded, for
                // example while a device is spun down. Such an item is skipped rather than treated as
                // a zero, which would claim the device was idle.
                DWORD const statusField = items[i].FmtValue.CStatus;
                if (statusField != ERROR_SUCCESS && statusField != static_cast<DWORD>(PDH_CSTATUS_VALID_DATA) &&
                    statusField != static_cast<DWORD>(PDH_CSTATUS_NEW_DATA))
                {
                    continue;
                }

                double const value = items[i].FmtValue.doubleValue;
                if (!std::isfinite(value))
                {
                    continue;
                }

                out.push_back(InstanceValue{_toNarrow(items[i].szName), value});
            }

            return !out.empty();
        }
    }

    HardwareCounterProbe::HardwareCounterProbe()
    {
        _openGpu();
    }

    HardwareCounterProbe::~HardwareCounterProbe()
    {
        _close();
    }

    void HardwareCounterProbe::_close() noexcept
    {
        if (m_gpuQuery != nullptr)
        {
            PdhCloseQuery(static_cast<PDH_HQUERY>(m_gpuQuery));
            m_gpuQuery = nullptr;
            m_gpuEngineCounter = nullptr;
            m_gpuMemoryCounter = nullptr;
            m_gpuSharedCounter = nullptr;
        }
    }

    bool _volumeHostsPageFile(wchar_t driveLetter)
    {
        // The page file's location is recorded in the session manager's key as a list of entries
        // shaped "C:\pagefile.sys 0 0". Only the drive letter is needed here.
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Memory Management",
                          0,
                          KEY_READ,
                          &key) != ERROR_SUCCESS)
        {
            return false;
        }

        wchar_t value[1024]{};
        DWORD valueBytes = sizeof(value);
        DWORD type = 0;
        bool hosts = false;

        if (RegQueryValueExW(key, L"PagingFiles", nullptr, &type, reinterpret_cast<LPBYTE>(value), &valueBytes) ==
                ERROR_SUCCESS &&
            type == REG_MULTI_SZ)
        {
            // The value is a double-null-terminated list of strings.
            for (wchar_t const* entry = value; *entry != L'\0'; entry += wcslen(entry) + 1)
            {
                if (towupper(entry[0]) == towupper(driveLetter) && entry[1] == L':')
                {
                    hosts = true;
                    break;
                }
            }
        }

        RegCloseKey(key);
        return hosts;
    }

    Result<std::vector<SystemDiskCounters>> HardwareCounterProbe::ReadDisks()
    {
        std::vector<SystemDiskCounters> disks;

        // The device list comes from PDH, but only for the instance names: they carry the device
        // index and the drive letters, which is what identifies a device to the user and what opens
        // the handle that reads its counters. The counter values come from IOCTL_DISK_PERFORMANCE
        // below, because PDH's PhysicalDisk counters are pre-computed rates while the Domain layer
        // differences cumulative values.
        DWORD counterBufferSize = 0;
        DWORD instanceBufferSize = 0;
        PDH_STATUS status = PdhEnumObjectItemsW(nullptr,
                                                nullptr,
                                                L"PhysicalDisk",
                                                nullptr,
                                                &counterBufferSize,
                                                nullptr,
                                                &instanceBufferSize,
                                                PERF_DETAIL_WIZARD,
                                                0);

        // Every early return here used to hand back an empty list as a success. Nothing upstream could
        // tell that apart from a machine with no disks, and the sidebar builds one row per device, so a
        // single enumeration failure made every device row disappear and come back. A failure to
        // enumerate is now reported as a failure, which leaves the previous reading standing.
        if (static_cast<DWORD>(status) != PDH_MORE_DATA || instanceBufferSize == 0)
        {
            return Error{ErrorCode::NativeFailure,
                         "the physical disk list could not be enumerated, PDH status " + _pdhError(status),
                         "HardwareCounterProbe::ReadDisks"};
        }

        std::vector<wchar_t> counterBuffer(counterBufferSize > 0 ? counterBufferSize : 1);
        std::vector<wchar_t> instanceBuffer(instanceBufferSize);
        DWORD counterSize = counterBufferSize;
        DWORD instanceSize = instanceBufferSize;

        status = PdhEnumObjectItemsW(nullptr,
                                     nullptr,
                                     L"PhysicalDisk",
                                     counterBuffer.data(),
                                     &counterSize,
                                     instanceBuffer.data(),
                                     &instanceSize,
                                     PERF_DETAIL_WIZARD,
                                     0);
        if (status != ERROR_SUCCESS)
        {
            return Error{ErrorCode::NativeFailure,
                         "the physical disk list could not be enumerated, PDH status " + _pdhError(status),
                         "HardwareCounterProbe::ReadDisks"};
        }

        // The instance names are a double-null-terminated list.
        wchar_t const* instance = instanceBuffer.data();
        while (*instance != L'\0')
        {
            std::wstring const instanceName{instance};
            instance += instanceName.length() + 1;

            if (instanceName == AGGREGATE_INSTANCE)
            {
                continue;
            }

            std::optional<uint32_t> const deviceIndex = _parseDeviceIndex(instanceName);
            if (!deviceIndex)
            {
                continue;
            }

            // Query-only access, so no administrator rights are needed. A failure here means the
            // device cannot report performance data at all, and it is skipped rather than reported as
            // a zero, which would look like a real reading of an idle disk.
            std::wstring const devicePath = L"\\\\.\\PhysicalDrive" + std::to_wstring(*deviceIndex);
            HANDLE const handle = CreateFileW(devicePath.c_str(),
                                              0,
                                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                                              nullptr,
                                              OPEN_EXISTING,
                                              0,
                                              nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                continue;
            }

            DISK_PERFORMANCE performance{};
            DWORD bytesReturned = 0;
            BOOL const queried = DeviceIoControl(handle,
                                                 IOCTL_DISK_PERFORMANCE,
                                                 nullptr,
                                                 0,
                                                 &performance,
                                                 sizeof(performance),
                                                 &bytesReturned,
                                                 nullptr);
            if (queried == FALSE)
            {
                CloseHandle(handle);
                continue;
            }

            SystemDiskCounters disk;
            disk.instanceName = _toNarrow(instanceName.c_str());
            disk.deviceIndex = *deviceIndex;
            disk.available = true;

            // IOCTL_DISK_PERFORMANCE genuinely reports cumulative values, which is what the Domain
            // layer's differencing expects.
            disk.readBytes = _clampNonNegative(performance.BytesRead.QuadPart);
            disk.writeBytes = _clampNonNegative(performance.BytesWritten.QuadPart);
            disk.readCount = performance.ReadCount;
            disk.writeCount = performance.WriteCount;

            // ReadTime, WriteTime and IdleTime are cumulative in 100-nanosecond units.
            disk.readTimeMs = _clampNonNegative(performance.ReadTime.QuadPart) / 10000ull;
            disk.writeTimeMs = _clampNonNegative(performance.WriteTime.QuadPart) / 10000ull;
            disk.idleTimeMs = _clampNonNegative(performance.IdleTime.QuadPart) / 10000ull;

            // The average request size is a mean, so it is derived rather than differenced: the mean
            // over an interval is not the difference of two means.
            disk.averageReadBytes =
                performance.ReadCount == 0
                    ? 0u
                    : static_cast<uint32_t>(disk.readBytes / static_cast<uint64_t>(performance.ReadCount));
            disk.averageWriteBytes =
                performance.WriteCount == 0
                    ? 0u
                    : static_cast<uint32_t>(disk.writeBytes / static_cast<uint64_t>(performance.WriteCount));

            // DISK_PERFORMANCE carries no sector size; the byte counts above are already bytes, and
            // 512 is the unit its SplitCount and request sizes are expressed in.
            disk.sectorSize = 512u;
            disk.queueDepth = performance.QueueDepth;

            // The device's model comes from its storage descriptor, which is optional: a device that
            // does not answer this still reports its counters, so a failure is not fatal.
            STORAGE_PROPERTY_QUERY propertyQuery{};
            propertyQuery.PropertyId = StorageDeviceProperty;
            propertyQuery.QueryType = PropertyStandardQuery;

            STORAGE_DESCRIPTOR_HEADER header{};
            DWORD headerBytes = 0;
            if (DeviceIoControl(handle,
                                IOCTL_STORAGE_QUERY_PROPERTY,
                                &propertyQuery,
                                sizeof(propertyQuery),
                                &header,
                                sizeof(header),
                                &headerBytes,
                                nullptr) != FALSE &&
                header.Size > 0 && header.Size < 4096)
            {
                std::vector<uint8_t> descriptor(header.Size);
                DWORD descriptorBytes = 0;
                if (DeviceIoControl(handle,
                                    IOCTL_STORAGE_QUERY_PROPERTY,
                                    &propertyQuery,
                                    sizeof(propertyQuery),
                                    descriptor.data(),
                                    header.Size,
                                    &descriptorBytes,
                                    nullptr) != FALSE)
                {
                    auto const* deviceDescriptor = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(descriptor.data());
                    if (deviceDescriptor->ProductIdOffset != 0 &&
                        deviceDescriptor->ProductIdOffset < descriptor.size())
                    {
                        // The descriptor's strings are single-byte and already null-terminated.
                        disk.modelName = std::string{reinterpret_cast<char const*>(
                            descriptor.data() + deviceDescriptor->ProductIdOffset)};
                    }

                    // The bus type rides along in the same descriptor, so it costs no extra query.
                    disk.busType = static_cast<uint32_t>(deviceDescriptor->BusType);
                }
            }

            // Whether seeking costs the device time, which is what separates a solid-state device
            // from a spinning one. This is the query the operating system uses for the same purpose,
            // so it is reliable where a model string is not.
            {
                STORAGE_PROPERTY_QUERY seekQuery{};
                seekQuery.PropertyId = StorageDeviceSeekPenaltyProperty;
                seekQuery.QueryType = PropertyStandardQuery;

                DEVICE_SEEK_PENALTY_DESCRIPTOR seekDescriptor{};
                DWORD seekBytes = 0;
                if (DeviceIoControl(handle,
                                    IOCTL_STORAGE_QUERY_PROPERTY,
                                    &seekQuery,
                                    sizeof(seekQuery),
                                    &seekDescriptor,
                                    sizeof(seekDescriptor),
                                    &seekBytes,
                                    nullptr) != FALSE)
                {
                    disk.incursSeekPenalty = (seekDescriptor.IncursSeekPenalty != FALSE);
                }
            }

            // TRIM support, which only solid-state devices report. A second clue for the same
            // question, used when the seek-penalty query is unavailable.
            {
                STORAGE_PROPERTY_QUERY trimQuery{};
                trimQuery.PropertyId = StorageDeviceTrimProperty;
                trimQuery.QueryType = PropertyStandardQuery;

                DEVICE_TRIM_DESCRIPTOR trimDescriptor{};
                DWORD trimBytes = 0;
                if (DeviceIoControl(handle,
                                    IOCTL_STORAGE_QUERY_PROPERTY,
                                    &trimQuery,
                                    sizeof(trimQuery),
                                    &trimDescriptor,
                                    sizeof(trimDescriptor),
                                    &trimBytes,
                                    nullptr) != FALSE)
                {
                    disk.trimEnabled = (trimDescriptor.TrimEnabled != FALSE);
                }
            }

            // Capacity comes from the first volume on the device rather than from
            // IOCTL_DISK_GET_LENGTH_INFO. That ioctl needs a handle opened with read access, and this
            // one is opened query-only so that no administrator rights are required; asking it
            // silently returned zero, which is how every device reported a capacity of 0 GB.
            //
            // The instance name already carries the drive letters, so the volume is right there.
            // A device backing several volumes reports the first one's capacity, which is the figure
            // a user recognises from Explorer.
            for (wchar_t const* letter = instanceName.c_str(); *letter != L'\0'; ++letter)
            {
                if (*letter < L'A' || *letter > L'Z')
                {
                    continue;
                }

                std::wstring const root = std::wstring{*letter} + L":\\";
                ULARGE_INTEGER freeForCaller{};
                ULARGE_INTEGER total{};
                ULARGE_INTEGER free{};
                if (GetDiskFreeSpaceExW(root.c_str(), &freeForCaller, &total, &free) != FALSE)
                {
                    disk.capacityBytes = total.QuadPart;

                    // The filesystem and the volume's label come from the same volume, through a call
                    // that takes the root path. Both are named in the original's details.
                    wchar_t volumeName[MAX_PATH + 1]{};
                    wchar_t fileSystemName[MAX_PATH + 1]{};
                    if (GetVolumeInformationW(root.c_str(),
                                              volumeName,
                                              static_cast<DWORD>(std::size(volumeName)),
                                              nullptr,
                                              nullptr,
                                              nullptr,
                                              fileSystemName,
                                              static_cast<DWORD>(std::size(fileSystemName))) != FALSE)
                    {
                        disk.volumeLabel = _toNarrow(volumeName);
                        disk.fileSystem = _toNarrow(fileSystemName);
                    }
                }

                // The page file's location comes from the registry rather than from the volume, and
                // it is a machine-wide list rather than a per-volume one.
                disk.hostsPageFile = _volumeHostsPageFile(*letter);
                break;
            }

            CloseHandle(handle);
            disks.push_back(std::move(disk));
        }

        // A device that cannot be opened is skipped above, so reaching here with nothing means either no
        // disks or none that report performance data. The difference matters to the caller, which is why
        // the availability flag is separate from the list being empty.
        m_disksAvailable = !disks.empty();
        return disks;
    }

    bool HardwareCounterProbe::_enumerateInterfaceIndices()
    {
        m_interfaceIndices.clear();

        // GetIfTable2 is the only call that enumerates reliably, but it asks every adapter's driver
        // for its row, including the ones that are not present. On this machine that is 76 entries
        // and 433 ms, which is most of a one-second sampling interval and was the cause of the
        // sampler falling behind.
        //
        // It is therefore used once, to learn which indices exist. Each sample then polls those
        // indices individually with GetIfEntry2, which touches one driver and measures at 2 ms.
        PMIB_IF_TABLE2 table = nullptr;
        if (GetIfTable2(&table) != NO_ERROR || table == nullptr)
        {
            return false;
        }

        auto const cleanup = std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)>(table, &FreeMibTable);

        for (ULONG i = 0; i < table->NumEntries; ++i)
        {
            MIB_IF_ROW2 const& row = table->Table[i];

            // Interfaces the driver reports as absent are left out, and this is the whole point of the
            // filter: GetIfEntry2 on one of them costs about a hundred and fifty times what it costs on a
            // live adapter, so polling them every sample added roughly two and a half seconds to a reading
            // that should take twenty milliseconds -- more than the sampling interval, which is what made
            // the sampler fall behind. A machine with a dozen virtual and disconnected adapters, which is
            // an ordinary machine, is where the cost was.
            //
            // Only "not present" is excluded, not "not up". A wireless adapter that is currently
            // disconnected is present and has to stay in the list: it is the interface whose state change
            // to up the page exists to notice, and dropping it would mean connecting to a network never
            // showed up.
            if (row.OperStatus == IfOperStatusNotPresent)
            {
                continue;
            }

            // Loopback and tunnel interfaces carry no user traffic to report, and the page has nothing to
            // say about them. The allow-list applied to each sample's results already excludes their types;
            // leaving them out here saves polling them at all.
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL)
            {
                continue;
            }

            // The filter adapters are excluded here rather than only after the query, and this is the fix
            // for the sampler falling behind.
            //
            // The per-sample loop already skips them -- they report the same traffic as the adapter beneath
            // them, so counting them would count every packet several times -- but it skipped them after
            // calling GetIfEntry2, and on this machine two of them block for about two and a half seconds:
            // the Npcap packet driver and the WFP lightweight filter layered on an NDIS internet-sharing
            // device. Measured over forty rounds, those two were the only adapters of seventy-six to exceed
            // a millisecond, and each stalled on roughly one call in thirteen. Their mean was 187 ms against
            // a twentieth of a millisecond for everything else, which is the two seconds that a network read
            // was taking.
            //
            // Excluding them here cannot change which adapters are reported: the same flag is tested against
            // the same row, only earlier, so the set that survives is identical and the query that used to
            // precede the test is not made at all.
            if (row.InterfaceAndOperStatusFlags.FilterInterface != 0)
            {
                continue;
            }

            m_interfaceIndices.push_back(static_cast<uint32_t>(row.InterfaceIndex));
        }

        return !m_interfaceIndices.empty();
    }

    Result<std::vector<SystemNetworkCounters>> HardwareCounterProbe::ReadNetwork()
    {
        std::vector<SystemNetworkCounters> interfaces;

        // Enumerated once for the lifetime of the probe. The set of adapters does not need to be
        // tracked live: a machine's network hardware does not change during a session, and the
        // enumeration costs hundreds of milliseconds because it queries every driver, including the
        // ones that are not present.
        if (!m_interfacesEnumerated)
        {
            m_interfacesEnumerated = true;
            if (!_enumerateInterfaceIndices())
            {
                return Error{ErrorCode::NativeFailure,
                             "no network interfaces could be enumerated",
                             "HardwareCounterProbe::ReadNetwork"};
            }
        }

        // An empty cache after a successful enumeration means the machine genuinely reports no
        // interfaces. Reaching here with a populated cache and no results is different: every adapter
        // failed to answer, which is a failure to observe rather than an observation of nothing.
        if (m_interfaceIndices.empty())
        {
            return interfaces;
        }

        uint32_t unanswered = 0;

        for (uint32_t const interfaceIndex : m_interfaceIndices)
        {
            MIB_IF_ROW2 row{};
            row.InterfaceIndex = interfaceIndex;

            if (GetIfEntry2(&row) != NO_ERROR)
            {
                // The adapter did not answer. It is skipped for this sample: rebuilding the list here is
                // what put a multi-hundred-millisecond enumeration back into every sample. The count is
                // kept so that a sample where nothing answered can be reported as the failure it is
                // rather than as a machine with no network.
                ++unanswered;
                continue;
            }

            // An allow-list of interface types rather than a deny-list. Enumerating the kinds that
            // cannot carry user traffic is a list that is never complete: the first attempt excluded
            // loopback and tunnels and still reported nine interfaces, among them the WAN Miniports
            // and a Hyper-V virtual switch, none of which is a connection the user has. Naming the
            // kinds that can carry traffic is a closed set.
            bool const canCarryTraffic = row.Type == IF_TYPE_ETHERNET_CSMACD || row.Type == IF_TYPE_IEEE80211 ||
                                         row.Type == IF_TYPE_PPP || row.Type == IF_TYPE_PROP_VIRTUAL;
            if (!canCarryTraffic)
            {
                continue;
            }

            // FilterInterface marks the lightweight filter adapters layered on a real one. They
            // report the same traffic as the adapter beneath them, so counting them would count every
            // packet several times.
            if (row.InterfaceAndOperStatusFlags.FilterInterface != 0)
            {
                continue;
            }

            // A reported link speed is what separates a real connection from a virtual endpoint. The
            // WAN Miniports report IF_TYPE_PPP, so the type list admits them, and they are up with no
            // traffic; what they never have is a link rate. Requiring one is a check the type list
            // cannot make.
            if (row.ReceiveLinkSpeed == 0 && row.TransmitLinkSpeed == 0)
            {
                continue;
            }

            // An adapter that has never carried a packet and is down is not a connection: reporting
            // it would add an idle row for hardware the user is not using.
            if (row.OperStatus != IfOperStatusUp && row.InOctets == 0 && row.OutOctets == 0)
            {
                continue;
            }

            SystemNetworkCounters counters;
            counters.receivedBytes = row.InOctets;
            counters.sentBytes = row.OutOctets;
            counters.receivedPackets = row.InUcastPkts + row.InNUcastPkts;
            counters.sentPackets = row.OutUcastPkts + row.OutNUcastPkts;
            counters.receiveErrors = row.InErrors;
            counters.sendErrors = row.OutErrors;
            counters.receiveDiscards = row.InDiscards;
            counters.sendDiscards = row.OutDiscards;
            counters.receiveLinkSpeedBps = row.ReceiveLinkSpeed;
            counters.transmitLinkSpeedBps = row.TransmitLinkSpeed;
            counters.interfaceType = row.Type;
        counters.connected = (row.OperStatus == IfOperStatusUp);
            counters.virtualAdapter = (row.InterfaceAndOperStatusFlags.HardwareInterface == 0);

            // The description is the marketing name; the alias is the name the user gave the
            // connection. Preferring the description matches what the original shows.
            counters.adapterName = row.Description[0] == L'\0' ? _toNarrow(row.Alias) : _toNarrow(row.Description);
            counters.available = true;

            interfaces.push_back(std::move(counters));
        }

        // A physical adapter is what a user means by their network. A virtual switch is a real
        // interface but not the connection, and listing both buries the one that matters. The virtual
        // ones are kept only when no physical adapter was found, which is what happens inside a
        // virtual machine.
        bool const anyPhysical = std::any_of(interfaces.begin(), interfaces.end(), [](SystemNetworkCounters const& entry) {
            return !entry.virtualAdapter;
        });

        if (anyPhysical)
        {
            std::erase_if(interfaces, [](SystemNetworkCounters const& entry) { return entry.virtualAdapter; });
        }

        // Nothing answered at all, although the enumeration says adapters exist. Reporting an empty list
        // would wipe every network row; reporting a failure leaves the previous reading standing.
        if (interfaces.empty() && unanswered > 0)
        {
            return Error{ErrorCode::NativeFailure,
                         "no network interface answered the counter query",
                         "HardwareCounterProbe::ReadNetwork"};
        }

        m_networkAvailable = !interfaces.empty();
        return interfaces;
    }

    void HardwareCounterProbe::_openGpu()
    {
        PDH_HQUERY query = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
        {
            return;
        }

        PDH_HCOUNTER engine = nullptr;
        PDH_HCOUNTER memory = nullptr;

        PDH_HCOUNTER shared = nullptr;

        bool const engineOk = PdhAddEnglishCounterW(query, GPU_ENGINE_COUNTER, 0, &engine) == ERROR_SUCCESS;
        bool const memoryOk = PdhAddEnglishCounterW(query, GPU_MEMORY_COUNTER, 0, &memory) == ERROR_SUCCESS;
        PdhAddEnglishCounterW(query, GPU_SHARED_COUNTER, 0, &shared);

        if (!engineOk && !memoryOk)
        {
            // No GPU counters on this system, which is normal in a virtual machine and on some older
            // drivers. The GPU row then shows a blank rather than a zero.
            PdhCloseQuery(query);
            return;
        }

        // Prime the query: these are rate counters, so the first collection establishes the baseline
        // the next one is differenced against and the first read is otherwise invalid.
        PdhCollectQueryData(query);

        m_gpuQuery = query;
        m_gpuEngineCounter = engine;
        m_gpuMemoryCounter = memory;
        m_gpuSharedCounter = shared;
        m_gpuAvailable = true;
    }

    void HardwareCounterProbe::_resolveGpuTotals() const
    {
        if (m_gpuTotalsResolved)
        {
            return;
        }
        m_gpuTotalsResolved = true;

        // The adapter's name and its dedicated memory total come from DXGI, which is the source the
        // operating system's own tools use.
        //
        // The registry was tried first and is not authoritative: its qwMemorySize reported 3.98 GB
        // for a card whose usage counter reported 5.95 GB, a driver-dependent disagreement that made
        // the figure impossible to present. DXGI reports what the adapter itself declares.
        //
        // The discrete adapter is preferred over the integrated one, because on a machine that has
        // both it is the one whose load is worth showing. DXGI enumerates in that order on such a
        // machine, but the choice is made explicitly rather than relied upon.
        Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory)))
        {
            return;
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter1> best;
        DXGI_ADAPTER_DESC1 bestDesc{};

        for (UINT index = 0;; ++index)
        {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND)
            {
                break;
            }

            DXGI_ADAPTER_DESC1 desc{};
            if (FAILED(adapter->GetDesc1(&desc)))
            {
                continue;
            }

            // The software adapter is the operating system's own renderer of last resort; it holds no
            // dedicated memory and is not what a user means by "the GPU".
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
            {
                continue;
            }

            // Prefer the adapter with dedicated memory, which is the discrete one.
            if (best == nullptr || (bestDesc.DedicatedVideoMemory == 0 && desc.DedicatedVideoMemory > 0))
            {
                best = adapter;
                bestDesc = desc;
            }

            if (bestDesc.DedicatedVideoMemory > 0)
            {
                break;
            }
        }

        if (best != nullptr)
        {
            m_gpuName = _toNarrow(bestDesc.Description);
            m_gpuDedicatedTotal = bestDesc.DedicatedVideoMemory;
        }

        // The driver version is a registry value rather than anything DXGI reports. It is read from
        // the display-device class key, whose subkeys are numbered per adapter.
        HKEY classKey = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}",
                          0,
                          KEY_READ,
                          &classKey) == ERROR_SUCCESS)
        {
            for (DWORD index = 0; index < 32; ++index)
            {
                wchar_t subKeyName[32]{};
                swprintf_s(subKeyName, L"%04u", index);

                HKEY subKey = nullptr;
                if (RegOpenKeyExW(classKey, subKeyName, 0, KEY_READ, &subKey) != ERROR_SUCCESS)
                {
                    continue;
                }

                wchar_t version[128]{};
                DWORD versionBytes = sizeof(version);
                if (RegQueryValueExW(subKey, L"DriverVersion", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(version), &versionBytes) == ERROR_SUCCESS)
                {
                    m_gpuDriverVersion = _toNarrow(version);
                }

                RegCloseKey(subKey);

                if (!m_gpuDriverVersion.empty())
                {
                    break;
                }
            }

            RegCloseKey(classKey);
        }
    }

    Result<SystemGpuInfo> HardwareCounterProbe::ReadGpu()
    {
        SystemGpuInfo info;

        if (!m_gpuAvailable || m_gpuQuery == nullptr)
        {
            return info;
        }

        _resolveGpuTotals();

        auto* const query = static_cast<PDH_HQUERY>(m_gpuQuery);
        PDH_STATUS const collectStatus = PdhCollectQueryData(query);
        if (collectStatus != ERROR_SUCCESS)
        {
            return Error{ErrorCode::NativeFailure,
                         "PdhCollectQueryData failed for the GPU with " + _pdhError(collectStatus),
                         "HardwareCounterProbe::ReadGpu"};
        }

        info.adapterName = m_gpuName;
        info.driverVersion = m_gpuDriverVersion;
        info.dedicatedTotalBytes = m_gpuDedicatedTotal;

        // --- Utilisation ------------------------------------------------------
        //
        // The GPU Engine counters are published per engine per process, so one adapter produces
        // dozens of instances such as
        // "pid_1234_luid_0x00000000_0x0000C1C3_phys_0_eng_0_engtype_3D". A single figure has to be
        // derived from them, and the original's figure is the busiest engine rather than the sum:
        // summing would report above 100 percent when several engines are each partly busy, and no
        // engine is ever more than fully busy.
        //
        // Summing within an engine first is what makes it correct. Several processes share each
        // engine, and those contributions do add up.
        if (m_gpuEngineCounter != nullptr)
        {
            std::vector<InstanceValue> engineValues;
            if (_readCounterArray(static_cast<PDH_HCOUNTER>(m_gpuEngineCounter), engineValues))
            {
                std::map<std::string, double> perEngine;

                for (InstanceValue const& value : engineValues)
                {
                    // The instance name ends in "_engtype_<TYPE>", which identifies the engine.
                    // Everything before it, including the process id, is deliberately dropped so that
                    // one engine's work across several processes is combined.
                    size_t const marker = value.instance.rfind("_engtype_");
                    if (marker == std::string::npos)
                    {
                        continue;
                    }

                    perEngine[value.instance.substr(marker)] += value.value;
                }

                // Each engine class is reported separately as well as reduced to the busiest. The
                // instance suffix is the engine's type name, which is what identifies the class.
                auto engineValue = [&perEngine](char const* typeName) {
                    for (auto const& engine : perEngine)
                    {
                        if (engine.first.find(typeName) != std::string::npos)
                        {
                            return std::clamp(engine.second, 0.0, 100.0);
                        }
                    }
                    return 0.0;
                };

                info.engine3dPercent = engineValue("3D");
                info.engineCopyPercent = engineValue("Copy");
                info.engineVideoDecodePercent = engineValue("VideoDecode");
                info.engineVideoEncodePercent = engineValue("VideoEncode");

                double busiest = 0.0;
                for (auto const& engine : perEngine)
                {
                    busiest = (std::max)(busiest, engine.second);
                }

                info.utilizationPercent = std::clamp(busiest, 0.0, 100.0);
                info.available = true;
            }
        }

        // --- Dedicated memory -------------------------------------------------
        if (m_gpuMemoryCounter != nullptr)
        {
            std::vector<InstanceValue> memoryValues;
            if (_readCounterArray(static_cast<PDH_HCOUNTER>(m_gpuMemoryCounter), memoryValues))
            {
                // The instances are per process, so they are summed: the figure is how much of the
                // adapter's own memory is in use across everything running.
                double dedicated = 0.0;
                for (InstanceValue const& value : memoryValues)
                {
                    dedicated += value.value;
                }

                info.dedicatedUsedBytes = static_cast<uint64_t>((std::max)(0.0, dedicated));
                info.available = true;
            }
        }

        // --- Shared memory ----------------------------------------------------
        if (m_gpuSharedCounter != nullptr)
        {
            std::vector<InstanceValue> sharedValues;
            if (_readCounterArray(static_cast<PDH_HCOUNTER>(m_gpuSharedCounter), sharedValues))
            {
                double shared = 0.0;
                for (InstanceValue const& value : sharedValues)
                {
                    shared += value.value;
                }
                info.sharedUsedBytes = static_cast<uint64_t>((std::max)(0.0, shared));
            }
        }

        return info;
    }

    void HardwareCounterProbe::ReadProcessGpu(std::map<uint32_t, double>& out)
    {
        out.clear();

        if (!m_gpuAvailable || m_gpuEngineCounter == nullptr)
        {
            return;
        }

        // The query was already collected by ReadGpu, which the coordinator calls before this. Collecting
        // again would advance the counters twice within one sample, so one of the two readings would be
        // measuring an interval of nearly zero.
        auto* const query = static_cast<PDH_HQUERY>(m_gpuQuery);
        if (query == nullptr)
        {
            return;
        }

        std::vector<InstanceValue> engineValues;
        if (!_readCounterArray(static_cast<PDH_HCOUNTER>(m_gpuEngineCounter), engineValues))
        {
            return;
        }

        // One instance per process per engine, named
        // "pid_1234_luid_0x00000000_0x0000C1C3_phys_0_eng_0_engtype_3D". Both fields are read from that
        // name: the pid is the run of digits after the prefix, and the engine is what follows
        // "_engtype_".
        //
        // The figure reported per process is its busiest engine, matching how the adapter's total is
        // derived. Summing a process's engines would report above 100 percent for anything using several
        // at once, and no engine is ever more than fully busy.
        constexpr char const* PID_PREFIX = "pid_";
        constexpr size_t PID_PREFIX_LENGTH = 4;

        // Keyed by process and engine so that contributions to the same engine combine before the
        // reduction, which is what makes the reduction correct when an engine reports more than one
        // instance for a process, as it does with several physical adapters present.
        std::map<std::pair<uint32_t, std::string>, double> perEngine;

        for (InstanceValue const& value : engineValues)
        {
            if (value.instance.rfind(PID_PREFIX, 0) != 0)
            {
                continue;
            }

            size_t const digitsBegin = PID_PREFIX_LENGTH;
            size_t digitsEnd = digitsBegin;
            while (digitsEnd < value.instance.size() &&
                   std::isdigit(static_cast<unsigned char>(value.instance[digitsEnd])) != 0)
            {
                ++digitsEnd;
            }

            if (digitsEnd == digitsBegin)
            {
                continue;
            }

            size_t const marker = value.instance.rfind("_engtype_");
            if (marker == std::string::npos)
            {
                continue;
            }

            uint32_t pid = 0;
            for (size_t i = digitsBegin; i < digitsEnd; ++i)
            {
                pid = (pid * 10u) + static_cast<uint32_t>(value.instance[i] - '0');
            }

            perEngine[{pid, value.instance.substr(marker)}] += value.value;
        }

        for (auto const& entry : perEngine)
        {
            double& slot = out[entry.first.first];
            slot = (std::max)(slot, entry.second);
        }

        for (auto& entry : out)
        {
            entry.second = std::clamp(entry.second, 0.0, 100.0);
        }
    }
}
