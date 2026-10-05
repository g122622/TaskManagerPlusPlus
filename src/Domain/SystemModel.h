// System-wide derived state and the chart history series.
//
// This is the only place long-term numeric history is kept. The charts are
// system-level series (total CPU, memory, and later disk/network/GPU), so a
// single owner with fixed-capacity buffers keeps history memory constant no
// matter how long the application runs.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "Domain/RateMath.h"
#include "Domain/RingBuffer.h"
#include "Domain/SamplingConfig.h"
#include "Platform/SystemTypes.h"

namespace tmpp::domain
{
    /**
     * @brief Identifies a charted series.
     *
     * Adding a series here and nowhere else is intentional: the colour settings,
     * the chart legend and the history buffer are all driven off this enum, so a
     * new metric cannot be half-wired.
     */
    enum class SeriesId
    {
        CpuTotal = 0,
        MemoryUsed,
        Count,
    };

    /**
     * @brief One disk's activity over the last interval.
     */
    struct DiskActivity
    {
        /// Bytes per second over the interval.
        double readBytesPerSecond{0.0};
        double writeBytesPerSecond{0.0};

        /**
         * @brief Bytes read and written since the application started measuring this device.
         *
         * A running total rather than a rate, accumulated across samples by the model. The device's own
         * counters are cumulative since boot, which is not what is wanted here: the figure answers "how
         * much has this application seen this disk do", so it begins at zero when the application does.
         *
         * Only the total is kept, one per device. Keeping the samples as well would be a second history
         * to hold and would still have to be summed to get this.
         */
        double readBytesTotal{0.0};
        double writeBytesTotal{0.0};

        /// Share of the interval spent servicing requests.
        ///
        /// Derived as the change in read plus write service time over the elapsed time. Using the
        /// service times rather than wall-clock busy time means a device servicing overlapping
        /// requests can exceed 100 percent, which is why it is clamped: the figure is a share of one
        /// device's attention, and it cannot exceed all of it.
        double activePercent{0.0};

        /// Requests outstanding at the sample, which is a level rather than a total.
        uint32_t queueDepth{0};

        uint64_t capacityBytes{0};
        std::string modelName;
        std::string instanceName;
        uint32_t deviceIndex{0};

        /// The bus the device is attached to, so a solid-state device can be named as NVMe or SATA.
        uint32_t busType{0};

        /// Whether seeking costs the device time, which is what separates a spinning disk from a
        /// solid-state one.
        bool incursSeekPenalty{true};

        /// Whether the device reports TRIM support, which only solid-state devices have.
        bool trimEnabled{false};

        /// The filesystem on the device's first volume, e.g. "NTFS".
        std::string fileSystem;

        /// The volume label of the first volume, which is the name a user sees in Explorer.
        std::string volumeLabel;

        /// Whether a page file resides on any volume this device backs.
        bool hostsPageFile{false};

        /// Mean time to service a request over the interval, in milliseconds.
        ///
        /// Derived from the read and write service times over the operations completed. It is a mean
        /// rather than a total, so it is computed per interval from the deltas rather than
        /// differenced: the difference of two means is not the mean of the interval.
        double averageResponseMs{0.0};

        /// The device's type in the words the original uses.
        [[nodiscard]] std::string TypeName() const
        {
            if (incursSeekPenalty)
            {
                return "HDD";
            }

            // A solid-state device on the PCI Express bus is NVMe; anything else is SATA or USB.
            // The bus type is what the storage descriptor reports, so this needs no guessing.
            constexpr uint32_t BUS_TYPE_NVME = 0x11;
            return (busType == BUS_TYPE_NVME) ? "SSD (NVMe)" : "SSD (SATA)";
        }
    };

    /**
     * @brief One network interface's activity over the last interval.
     */
    struct NetworkActivity
    {
        double receivedBytesPerSecond{0.0};
        double sentBytesPerSecond{0.0};

        uint64_t receiveLinkSpeedBps{0};
        uint64_t transmitLinkSpeedBps{0};

        std::string adapterName;

        /// True when the adapter is wireless. Carried from the probe's interface type rather than
        /// inferred from the name: a description says which chipset it is, and only the type says
        /// whether the medium is radio or wire.
        bool wireless{false};

        bool connected{false};
        bool virtualAdapter{false};

        /// Cumulative error and discard counts, carried so the page can show them. They are totals
        /// rather than rates: an error count is a running tally of things that went wrong, and a
        /// per-second figure would imply a fault rate that reads as meaningless when it is zero.
        uint64_t receiveErrors{0};
        uint64_t sendErrors{0};
        uint64_t receiveDiscards{0};
        uint64_t sendDiscards{0};

        /// Share of the link in use, as a percentage of the slower of the two directions. Zero
        /// when the link speed is unknown, since a proportion needs a whole to be a proportion of.
        [[nodiscard]] double UtilizationPercent() const noexcept
        {
            uint64_t const link = (receiveLinkSpeedBps < transmitLinkSpeedBps) ? receiveLinkSpeedBps
                                                                               : transmitLinkSpeedBps;
            if (link == 0)
            {
                return 0.0;
            }
            double const busier = (receivedBytesPerSecond > sentBytesPerSecond) ? receivedBytesPerSecond
                                                                               : sentBytesPerSecond;
            // The link speed is in bits and the byte counters in bytes, so the conversion is part of
            // the comparison rather than an afterthought.
            return std::clamp((busier * 8.0 * 100.0) / static_cast<double>(link), 0.0, 100.0);
        }
    };

    /**
     * @brief One instant of system state, as published to the UI.
     */
    struct SystemView
    {
        uint64_t version{0};
        uint64_t capturedAt{0};
        double elapsedMs{0.0};

        // CPU
        double cpuPercent{0.0};
        std::vector<double> perProcessorCpuPercent;

        // Memory, in bytes.
        platform::SystemMemoryInfo memory;
        double memoryUsedPercent{0.0};
        uint64_t memoryUsedBytes{0};

        /// How physical memory is distributed across the page lists. Its available flag is
        /// false when the probe could not read it, in which case the composition bar is not
        /// drawn rather than being drawn empty.
        platform::SystemMemoryComposition memoryComposition;

        // Static topology, refreshed on the first sample only.
        platform::SystemProcessorInfo processor;

        /// Live clock speed. Changes every sample; unavailable on some systems.
        platform::SystemProcessorSpeed processorSpeed;

        /// Rolling system-wide totals: process/thread/handle counts and uptime.
        platform::SystemTotals totals;

        /// Per-device disk activity, with the rates already derived from the cumulative counters.
        /// Empty until a baseline exists, and empty on a machine whose disks cannot report
        /// performance data.
        std::vector<DiskActivity> disks;

        /// Network interfaces with their rates derived. Empty until a baseline exists.
        std::vector<NetworkActivity> networks;

        /// The GPU's utilisation and memory. Its available flag is false when the counters could not
        /// be read, so the row shows a blank rather than a zero.
        platform::SystemGpuInfo gpu;

        /// Mean active share across the devices that reported a rate this sample.
        double diskActivePercent{0.0};

        /// True until a baseline exists, so the UI can show a blank rather than 0.
        bool ratesUnavailable{true};
    };

    /**
     * @brief A snapshot of the charted history, oldest sample first.
     */
    struct HistoryView
    {
        std::vector<double> cpuTotal;
        std::vector<double> memoryUsed;

        /// Aggregate disk throughput across every device, in bytes per second. Two series rather than
        /// one because reads and writes are separately interesting, and the original plots them as
        /// separate lines.
        std::vector<double> diskReadBytesPerSecond;
        std::vector<double> diskWriteBytesPerSecond;

        /// Aggregate network throughput, in bytes per second.
        std::vector<double> networkReceiveBytesPerSecond;
        std::vector<double> networkSendBytesPerSecond;

        /// One device's throughput history, with the two directions kept apart.
        ///
        /// Kept apart rather than summed because a disk's reads and writes are separately interesting,
        /// and the page plots them as two lines. A single summed series cannot be split back into the
        /// two, which is what forced the page to fall back on the machine-wide aggregate.
        struct DirectionalHistory
        {
            std::vector<double> first;  ///< Reads, or received bytes for a network adapter.
            std::vector<double> second; ///< Writes, or sent bytes.
        };

        /// Per-device disk throughput, in bytes per second, keyed by the device's instance name.
        std::map<std::string, DirectionalHistory> diskBytesPerSecondByDevice;

        /// Per-adapter network throughput, in bytes per second, keyed by the adapter's name.
        std::map<std::string, DirectionalHistory> networkBytesPerSecondByAdapter;

        /// GPU utilisation, as a percentage.
        std::vector<double> gpuUtilization;

        /// Dedicated video memory in use, in bytes. A separate series from the utilisation because the
        /// two answer different questions: how hard the adapter is working, and how much of its memory
        /// is committed.
        std::vector<double> gpuDedicatedMemory;

        /// One series per logical processor, in processor order. Empty when the
        /// per-processor probe is unavailable.
        ///
        /// Each inner vector is the same length as cpuTotal, so a caller can index
        /// them in step. The per-core grid in the performance page needs this: it
        /// draws one sparkline per core, and a set of current values would render as a
        /// flat line with no history.
        std::vector<std::vector<double>> perProcessorCpu;

        /**
         * @brief Number of samples that represents the full time window.
         *
         * Carried with the data rather than queried separately because every chart needs
         * it and it is the same for all of them. When it was a separate setter, three of
         * the four charts were never told and drew their few samples stretched across the
         * entire width -- which reads as a settled history that does not exist yet, and
         * then visibly compresses as real samples arrive.
         */
        size_t windowSamples{0};

        [[nodiscard]] size_t SampleCount() const noexcept { return cpuTotal.size(); }
    };

    /**
     * @brief Derives system CPU/memory state and maintains chart history.
     *
     * Not thread-safe: the owner calls Update from a single sampling thread.
     */
    class SystemModel
    {
    public:
        /**
         * @param logicalProcessorCount Expected number of logical processors.
         * @param intervalMs Sampling interval; sizes the history buffers.
         * @param historySeconds History window; sizes the history buffers.
         */
        SystemModel(uint32_t logicalProcessorCount, uint32_t intervalMs, uint32_t historySeconds);

        /**
         * @brief Consumes raw CPU and memory readings and publishes a view.
         *
         * @param cpu Current cumulative system CPU times.
         * @param memory Current memory state.
         * @param capturedAt QPC tick count for this sample.
         * @param perProcessor Per-processor readings for this same sample, or an empty
         *        vector when that probe failed. Passed here rather than through a
         *        second call so that one sample advances every series exactly once:
         *        when the two were separate, a failed per-processor probe left the
         *        per-core series longer than the aggregate and the chart grid indexed
         *        them against the wrong axis.
         */
        void Update(platform::SystemCpuTimes const& cpu,
                    platform::SystemMemoryInfo const& memory,
                    uint64_t capturedAt,
                    std::vector<platform::ProcessorCpuTimes> const& perProcessor = {});

        /**
         * @brief Records the per-processor readings for the current sample.
         *
         * @deprecated Retained only so existing tests compile. Use the four-argument
         *        Update instead: this call advances the per-core rings without touching
         *        the aggregate, so calling it alongside Update makes the series drift
         *        apart. It will be removed once the tests are migrated.
         */
        void UpdatePerProcessor(std::vector<platform::ProcessorCpuTimes> const& perProcessor);

        /**
         * @brief Supplies the static processor description, read once at startup.
         */
        void SetProcessorInfo(platform::SystemProcessorInfo info);

        /**
         * @brief Records the live clock speed for the current sample.
         *
         * Separate from the topology because its source can fail independently of
         * everything else, and a missing speed must not disturb the other readings.
         */
        void SetProcessorSpeed(platform::SystemProcessorSpeed speed);

        /**
         * @brief Records the rolling system-wide totals.
         */
        void SetTotals(platform::SystemTotals totals);

        /**
         * @brief Records how physical memory is distributed across the page lists.
         */
        void SetMemoryComposition(platform::SystemMemoryComposition composition);

        /**
         * @brief Records the hardware counters for the current sample.
         *
         * Takes the cumulative disk and network counters and derives the rates by differencing against
         * the previous sample, exactly as the CPU figures are derived. Passing the raw counters rather
         * than pre-computed rates is what keeps every series on the same clock and the same interval:
         * a platform that did its own differencing would be measuring a different span.
         *
         * The GPU counters are already rates, so they are recorded as given.
         *
         * @param disks Cumulative disk counters.
         * @param networks Cumulative network counters.
         * @param gpu The GPU reading.
         * @param capturedAt The same timestamp passed to Update, so the interval matches.
         */
        void SetHardwareCounters(std::vector<platform::SystemDiskCounters> const& disks,
                                 std::vector<platform::SystemNetworkCounters> const& networks,
                                 platform::SystemGpuInfo const& gpu,
                                 uint64_t capturedAt);

        [[nodiscard]] SystemView const& Latest() const noexcept { return m_latest; }

        /// Copies the charted history, oldest sample first.
        [[nodiscard]] HistoryView History() const;

        [[nodiscard]] size_t HistoryCapacity() const noexcept { return m_historyCapacity; }

    private:
        /// Computes the per-processor percentages and pushes them into the per-core rings.
        ///
        /// Kept private and called only from Update, which is what guarantees the
        /// per-core series and the aggregate stay the same length.
        void _appendPerProcessor(std::vector<platform::ProcessorCpuTimes> const& perProcessor);

        uint32_t m_logicalProcessorCount{0};
        size_t m_historyCapacity{0};

        RingBuffer<double> m_cpuHistory;
        RingBuffer<double> m_memoryHistory;

        RingBuffer<double> m_diskReadHistory;
        RingBuffer<double> m_diskWriteHistory;

        /// One pair of rings per disk, keyed by instance name. Keyed rather than indexed because the
        /// device enumeration order is not guaranteed between samples, and a series attached to the
        /// wrong device is worse than no series.
        struct DirectionalRings
        {
            /// RingBuffer has no default constructor -- a buffer without a capacity has nowhere to
            /// store anything -- so the pair is constructed explicitly with the history capacity.
            DirectionalRings(size_t capacity) : first(capacity), second(capacity) {}

            RingBuffer<double> first;  ///< Reads, or received bytes.
            RingBuffer<double> second; ///< Writes, or sent bytes.
        };

        std::map<std::string, DirectionalRings> m_diskHistoryByDevice;

        /// One pair of rings per network adapter, keyed by name for the same reason.
        std::map<std::string, DirectionalRings> m_networkHistoryByAdapter;
        RingBuffer<double> m_networkReceiveHistory;
        RingBuffer<double> m_networkSendHistory;
        RingBuffer<double> m_gpuHistory;
        RingBuffer<double> m_gpuMemoryHistory;

        /// One ring per logical processor. Empty when the per-processor probe is
        /// unavailable, in which case no per-core chart can be drawn and the UI says
        /// so rather than showing a row of flat lines.
        std::vector<RingBuffer<double>> m_perProcessorHistory;

        SystemView m_latest;
        uint64_t m_version{0};

        /// The most recent per-processor percentages, published with each sample.
        ///
        /// Its length is always the per-core ring count rather than the probe's count, so
        /// a probe that reports fewer processors than the topology still yields series of
        /// equal length.
        std::vector<double> m_perProcessorPercent;

        platform::SystemCpuTimes m_previousCpu;
        std::vector<platform::ProcessorCpuTimes> m_previousPerProcessor;
        uint64_t m_previousCapturedAt{0};
        bool m_hasBaseline{false};

        /// Cumulative disk counters from the previous sample, keyed by instance name so a device is
        /// matched to itself rather than to whatever position it happened to hold last time.
        std::map<std::string, platform::SystemDiskCounters> m_previousDisks;

        /// Cumulative network counters from the previous sample, keyed by adapter name.
        std::map<std::string, platform::SystemNetworkCounters> m_previousNetworks;

        uint64_t m_previousHardwareCapturedAt{0};
        bool m_hasHardwareBaseline{false};

        /**
         * @brief Bytes read and written per device since the application started, keyed by instance name.
         *
         * One running total per device and nothing else: the rates already keep a window of samples, and
         * this is the figure that window cannot give because it is meant to cover the whole run rather
         * than the last minute.
         *
         * The pair is read then written, in that order.
         */
        std::map<std::string, std::pair<double, double>> m_diskBytesTotal;
    };
}
