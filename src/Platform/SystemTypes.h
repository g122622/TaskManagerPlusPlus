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

        // The kernel's own memory accounting, which GlobalMemoryStatusEx does not provide. Every figure
        // here is a byte count derived from GetPerformanceInfo, which reports in pages.

        /// Paged pool: kernel allocations that may be written to the page file.
        uint64_t pagedPoolBytes{0};

        /// Non-paged pool: kernel allocations that must stay resident.
        uint64_t nonPagedPoolBytes{0};

        /// The system file cache's size.
        uint64_t systemCacheBytes{0};

        /// The commit charge, its limit, and the highest it has been since boot.
        uint64_t committedBytes{0};
        uint64_t commitLimitBytes{0};
        uint64_t peakCommittedBytes{0};

        /// Kernel memory in total, which is the paged and non-paged pools plus the driver images.
        uint64_t kernelTotalBytes{0};

        /// Handles across every process, which GetPerformanceInfo reports without another enumeration.
        uint64_t systemHandleCount{0};

        /// True when the accounting above came from GetPerformanceInfo rather than being derived.
        bool kernelAccountingAvailable{false};
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
     * which is what lets the composition bar be drawn as one continuous strip. Compressed memory
     * is reported beside them without being a fifth category; see compressedBytes.
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

        /**
         * @brief Bytes held in the memory compression store.
         *
         * Windows keeps its compressed pages in a working set of their own, held by a process named
         * "Memory Compression", and this is that process's residency -- the figure Windows Task
         * Manager reports as compressed.
         *
         * It is a part of inUseBytes rather than a category beside it: the compressed store is
         * resident, so those pages are already counted in the in-use total. The strip draws this
         * length hatched at the leading edge of the in-use segment, and adding it to the categories
         * below would count the same bytes twice and make the strip longer than the machine's
         * memory.
         *
         * Zero when memory compression is off, which leaves no compression process to read, and on
         * a round whose process enumeration failed.
         */
        uint64_t compressedBytes{0};

        /// Page size used for the conversion, for diagnostics.
        uint64_t pageSize{0};

        /// True when the breakdown came from the page lists rather than being derived.
        bool available{false};

        /**
         * @brief Total accounted for, which should equal installed physical memory.
         *
         * Compressed memory is deliberately absent from the sum: it is already inside inUseBytes.
         */
        [[nodiscard]] uint64_t Total() const noexcept
        {
            return inUseBytes + modifiedBytes + standbyBytes + freeBytes;
        }
    };

    /**
     * @brief One physical disk's cumulative counters.
     *
     * Cumulative rather than rates, so the Domain layer derives every rate the same way it does for
     * CPU: by differencing two samples and dividing by the elapsed time. The alternative is to let
     * the platform's own rate counters do the differencing, which would leave the disk figures
     * computed on a different clock from everything else on the page and make the first sample
     * impossible to show honestly.
     *
     * These come from IOCTL_DISK_PERFORMANCE, which reports genuinely cumulative values. PDH's
     * PhysicalDisk counters look simpler but are pre-computed rates, and mixing the two conventions
     * in one struct is how a rate ends up differenced twice.
     */
    struct SystemDiskCounters
    {
        /// Cumulative bytes read since the counters started, including reads served from cache.
        uint64_t readBytes{0};

        /// Cumulative bytes written since the counters started.
        uint64_t writeBytes{0};

        /// Cumulative time spent servicing reads, in milliseconds.
        uint64_t readTimeMs{0};

        /// Cumulative time spent servicing writes, in milliseconds.
        uint64_t writeTimeMs{0};

        /// Cumulative time the device spent with no request outstanding, in milliseconds.
        ///
        /// This is the field active time is derived from. Read time plus write time is not a
        /// substitute: a device servicing overlapping requests accumulates both at once, so the sum
        /// exceeds the wall clock and reports a device as busier than it can be. Idle time cannot.
        uint64_t idleTimeMs{0};

        /// Completed operations, cumulative.
        uint64_t readCount{0};
        uint64_t writeCount{0};

        /// Average request size in bytes, reported directly because it is a mean rather than a
        /// total: differencing two means would not give the mean over the interval.
        uint32_t averageReadBytes{0};
        uint32_t averageWriteBytes{0};

        /// Nominal sector size, needed to interpret the byte counts on some drivers.
        uint32_t sectorSize{512};

        /// The bus the device is attached to, as the storage descriptor reports it. Used to say
        /// whether a solid-state device is NVMe or SATA, which the original distinguishes.
        uint32_t busType{0};

        /// Whether the device reports that seeking costs it time. A device that does not is
        /// solid-state; one that does is a spinning disk. This is the query the operating system
        /// itself uses, so it is reliable where the model string is not.
        bool incursSeekPenalty{true};

        /// Whether the device reports TRIM support, which only solid-state devices have.
        bool trimEnabled{false};

        /// The filesystem on the device's first volume, e.g. "NTFS".
        std::string fileSystem;

        /// The volume label of the first volume, which is the name a user sees in Explorer.
        std::string volumeLabel;

        /// Whether a page file resides on any volume this device backs.
        bool hostsPageFile{false};

        /// Requests outstanding at the moment of the query.
        ///
        /// A level rather than a total, so it is reported as read instead of being differenced: the
        /// difference of two queue depths is not a queue depth.
        uint32_t queueDepth{0};

        /// Device capacity in bytes, from the geometry query rather than from a volume, so it
        /// covers the whole device even when only part of it is mounted.
        uint64_t capacityBytes{0};

        /// The device's model string, e.g. "Samsung SSD 990 PRO 2TB".
        std::string modelName;

        /// The device's instance name as reported, e.g. "0 C: D:". Used to identify it between
        /// samples. Shown to the user, since it is what names the row.
        std::string instanceName;

        /// Index of the physical device this came from, parsed from the instance name.
        uint32_t deviceIndex{0};

        /// True when the counters were read successfully.
        bool available{false};
    };

    /**
     * @brief Cumulative counters for one network interface, in bytes.
     */
    struct SystemNetworkCounters
    {
        uint64_t receivedBytes{0};
        uint64_t sentBytes{0};
        uint64_t receivedPackets{0};
        uint64_t sentPackets{0};

        /// Bytes discarded because of an error or a full buffer. Cumulative, so a rate is derived by
        /// differencing like every other byte counter.
        uint64_t receiveErrors{0};
        uint64_t sendErrors{0};

        /// Packets discarded for lack of buffer space rather than because of an error. Kept apart
        /// from the errors, because a packet dropped under load and a corrupt one are different
        /// problems with different causes.
        uint64_t receiveDiscards{0};
        uint64_t sendDiscards{0};

        /// Receive and transmit link speed in bits per second, from the adapter.
        uint64_t receiveLinkSpeedBps{0};
        uint64_t transmitLinkSpeedBps{0};

        /// The adapter's description, e.g. "Intel(R) Ethernet Controller I225-V".
        std::string adapterName;

        /// True when the interface is operationally up.
        /// The interface's type, as IANAifType. Carried because it is what distinguishes a wireless
        /// adapter from a wired one: a description names the chipset rather than the medium, so
        /// "Intel(R) Wi-Fi 6E AX211" and "Intel(R) Ethernet Controller I225-V" are told apart by this
        /// and not by their names.
        uint32_t interfaceType{0};

        bool connected{false};

        /// True for a virtual adapter, such as a Hyper-V switch or a VPN tunnel. A real interface,
        /// but not the machine's connection, so it is hidden when a physical adapter is present.
        bool virtualAdapter{false};

        /// True when the counters were read successfully.
        bool available{false};
    };

    /**
     * @brief One GPU's utilisation and memory, as reported by the performance counters.
     *
     * Read through PDH rather than through a vendor API: the GPU Engine and GPU Adapter Memory
     * counter sets are provided by Windows itself for every WDDM adapter, so this works on any
     * GPU, including integrated ones and those whose vendor SDK is not installed.
     */
    struct SystemGpuInfo
    {
        /// Busiest engine's utilisation, as a percentage. A GPU runs several engines at once
        /// (3D, copy, video decode), and the original's single headline figure is the busiest of them.
        double utilizationPercent{0.0};

        /// Utilisation of each engine class, as a percentage.
        ///
        /// The original breaks the figure out this way because "the busiest engine" says how loaded
        /// the adapter is but not what it is being asked to do. A video call and a game can both
        /// report fifty percent while exercising entirely different hardware.
        ///
        /// Zero means that engine reported nothing this sample, which is normal when it is unused.
        double engine3dPercent{0.0};
        double engineCopyPercent{0.0};
        double engineVideoDecodePercent{0.0};
        double engineVideoEncodePercent{0.0};

        /// Dedicated video memory in use, in bytes. This is the GPU's own memory, not the shared
        /// system memory an integrated GPU borrows.
        uint64_t dedicatedUsedBytes{0};

        /// Total dedicated video memory, in bytes. Zero when the adapter has none, which is the
        /// case for integrated GPUs.
        uint64_t dedicatedTotalBytes{0};

        /// Shared system memory in use, in bytes. An integrated GPU borrows system memory, and even a
        /// discrete one uses it for some workloads, so this is not zero on either.
        uint64_t sharedUsedBytes{0};

        /// Total shared system memory available to the adapter, in bytes.
        uint64_t sharedTotalBytes{0};

        /// The driver version, as the registry records it.
        std::string driverVersion;

        /// The adapter's description, e.g. "NVIDIA GeForce RTX 4070".
        std::string adapterName;

        /// True when at least one counter was readable.
        bool available{false};
    };

    /**
     * @brief One memory module, as the firmware describes it.
     *
     * Read from the SMBIOS table rather than from an API, because no Windows API reports the modules:
     * the operating system knows how much memory there is, not what it is made of. This is the same
     * source the original's memory page uses.
     */
    struct SystemMemoryModule
    {
        /// The slot's designator, e.g. "DIMM_A1". Empty when the firmware does not name it.
        std::string slot;

        /// The module's part number, e.g. "CMK32GX5M2B5600C36".
        std::string partNumber;

        /// The manufacturer, e.g. "Corsair".
        std::string manufacturer;

        /// The module's serial number, as the firmware reports it.
        std::string serialNumber;

        /// Capacity in bytes, as the module declares.
        uint64_t capacityBytes{0};

        /// Configured clock in MHz, the speed the module is actually running at.
        uint32_t configuredSpeedMhz{0};

        /// The module's rated speed in MHz, which may be higher than the configured one.
        uint32_t ratedSpeedMhz{0};

        /// The SMBIOS memory type code, and its name as the original shows it.
        uint16_t typeCode{0};
        std::string typeName;

        /// The form factor code, and its name.
        uint8_t formFactorCode{0};
        std::string formFactorName;

        /// The width of the data bus in bits, which is what "x64" refers to.
        uint16_t dataWidthBits{0};

        /// The voltage in millivolts, zero when the firmware does not report it.
        uint32_t voltageMillivolts{0};

        /// True when the slot holds a module. Empty slots are reported too, so the page can say how
        /// many there are, which is what "Slots used: 2 of 4" means.
        bool populated{false};
    };

    /**
     * @brief The machine's memory slots, populated and empty.
     */
    struct SystemMemorySlots
    {
        std::vector<SystemMemoryModule> modules;

        /// Slots the firmware describes, including empty ones.
        uint32_t totalSlots{0};

        /// Slots holding a module.
        uint32_t usedSlots{0};

        /// True when the table was readable and describes at least one slot.
        bool available{false};
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

        /// True when at least one physical disk's counters were readable.
        bool hasDiskCounters{false};

        /// True when at least one network interface's counters were readable.
        bool hasNetworkCounters{false};

        /// True when the GPU performance counters were queryable.
        bool hasGpuCounters{false};
    };
}
