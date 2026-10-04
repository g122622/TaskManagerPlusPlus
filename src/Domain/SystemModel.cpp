#include "SystemModel.h"

#include <algorithm>

#include "Platform/Clock.h"

namespace tmpp::domain
{
    SystemModel::SystemModel(uint32_t logicalProcessorCount, uint32_t intervalMs, uint32_t historySeconds)
        : m_logicalProcessorCount(logicalProcessorCount == 0 ? 1 : logicalProcessorCount),
          m_historyCapacity(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_cpuHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_memoryHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_diskReadHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_diskWriteHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_networkReceiveHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_networkSendHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_gpuHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_gpuMemoryHistory(sampling::HistoryCapacity(intervalMs, historySeconds))
    {
        m_latest.perProcessorCpuPercent.reserve(m_logicalProcessorCount);

        // One history ring per logical processor, each with the same capacity as the
        // aggregate, so every series in a HistoryView has the same length and a caller
        // can index them in step. Allocated up front: the count is fixed for the
        // lifetime of the process and reallocating mid-run would invalidate the series.
        m_perProcessorHistory.reserve(m_logicalProcessorCount);
        for (uint32_t i = 0; i < m_logicalProcessorCount; ++i)
        {
            m_perProcessorHistory.emplace_back(sampling::HistoryCapacity(intervalMs, historySeconds));
        }
    }

    void SystemModel::Update(platform::SystemCpuTimes const& cpu,
                             platform::SystemMemoryInfo const& memory,
                             uint64_t capturedAt,
                             std::vector<platform::ProcessorCpuTimes> const& perProcessor)
    {
        double const elapsedMs = m_hasBaseline ? platform::MillisecondsBetween(m_previousCapturedAt, capturedAt) : 0.0;

        SystemView next;
        next.version = ++m_version;
        next.capturedAt = capturedAt;
        next.elapsedMs = elapsedMs;
        next.memory = memory;
        next.processor = m_latest.processor;
        next.processorSpeed = m_latest.processorSpeed;
        next.totals = m_latest.totals;

        // Memory state is instantaneous, so it is always available.
        next.memoryUsedBytes = (memory.totalPhysical > memory.availablePhysical)
                                   ? (memory.totalPhysical - memory.availablePhysical)
                                   : 0;
        next.memoryUsedPercent = ComputePercentage(next.memoryUsedBytes, memory.totalPhysical, limits::MAX_MEMORY_PERCENT).value;

        // Carried through from the previous publication; it arrives separately because it
        // comes from a different probe that can fail on its own.
        next.memoryComposition = m_latest.memoryComposition;

        // The hardware fields are carried through for the same reason, and it was a real defect that
        // they were not: this method replaces the whole view, and SetHardwareCounters does not
        // necessarily run before the UI reads it. Every sample therefore published a view whose device
        // lists were empty, and the sidebar showed "unavailable" against every disk until the next
        // hardware read happened to land first. The fields are short-lived -- overwritten by
        // SetHardwareCounters moments later in the same sample -- but "moments later" is not a
        // guarantee the reader can rely on.
        next.disks = m_latest.disks;
        next.networks = m_latest.networks;
        next.gpu = m_latest.gpu;
        next.diskActivePercent = m_latest.diskActivePercent;

        bool cpuDerived = false;
        if (m_hasBaseline && elapsedMs > 0.0)
        {
            // Kernel time includes idle, so the difference must be taken per field
            // before the busy fraction is computed. Differencing the totals first
            // would double-count idle.
            Delta const kernelDelta = ComputeDelta(m_previousCpu.kernelTime, cpu.kernelTime);
            Delta const idleDelta = ComputeDelta(m_previousCpu.idleTime, cpu.idleTime);
            Delta const userDelta = ComputeDelta(m_previousCpu.userTime, cpu.userTime);

            if (kernelDelta.Usable() && idleDelta.Usable() && userDelta.Usable())
            {
                uint64_t const idle = idleDelta.value;
                uint64_t const kernelBusy = (kernelDelta.value > idle) ? (kernelDelta.value - idle) : 0;
                uint64_t const busy = kernelBusy + userDelta.value;

                next.cpuPercent = ComputeSystemCpuPercent(busy, idle).value;
                cpuDerived = true;
            }
        }

        next.ratesUnavailable = !cpuDerived;

        // Per-processor readings are folded in here so that this single call advances
        // every series exactly once. When they arrived through a separate call, a failed
        // per-processor probe skipped its push and left the per-core series longer than
        // the aggregate, so the chart grid plotted them against the wrong axis.
        _appendPerProcessor(perProcessor);
        next.perProcessorCpuPercent = m_latest.perProcessorCpuPercent;

        // History holds placeholder values until a baseline exists, so the chart keeps a
        // consistent time axis from the first frame rather than jumping.
        m_cpuHistory.Push(cpuDerived ? next.cpuPercent : 0.0);
        m_memoryHistory.Push(next.memoryUsedPercent);

        m_latest = std::move(next);
        m_latest.perProcessorCpuPercent = m_perProcessorPercent;
        m_previousCpu = cpu;
        m_previousCapturedAt = capturedAt;
        m_hasBaseline = true;
    }

    void SystemModel::_appendPerProcessor(std::vector<platform::ProcessorCpuTimes> const& perProcessor)
    {
        std::vector<double> percentages;
        percentages.reserve(m_perProcessorHistory.size());

        if (perProcessor.empty())
        {
            // The probe failed. The rings still advance, so every series keeps the same
            // length as the aggregate; the values are zero, which the UI shows as a
            // blank rather than as a measured idle.
            percentages.assign(m_perProcessorHistory.size(), 0.0);
            m_previousPerProcessor.clear();

            for (auto& ring : m_perProcessorHistory)
            {
                ring.Push(0.0);
            }
            m_perProcessorPercent = std::move(percentages);
            return;
        }

        bool const comparable = m_previousPerProcessor.size() == perProcessor.size();
        size_t const count = std::min(perProcessor.size(), m_previousPerProcessor.size());

        percentages.resize(perProcessor.size(), 0.0);

        for (size_t i = 0; i < perProcessor.size(); ++i)
        {
            if (!comparable || i >= count)
            {
                // Without a matching previous reading this processor has no rate yet.
                continue;
            }

            auto const& before = m_previousPerProcessor[i];
            auto const& now = perProcessor[i];

            Delta const kernelDelta = ComputeDelta(before.kernelTime, now.kernelTime);
            Delta const idleDelta = ComputeDelta(before.idleTime, now.idleTime);
            Delta const userDelta = ComputeDelta(before.userTime, now.userTime);

            if (!kernelDelta.Usable() || !idleDelta.Usable() || !userDelta.Usable())
            {
                continue;
            }

            uint64_t const idle = idleDelta.value;
            uint64_t const kernelBusy = (kernelDelta.value > idle) ? (kernelDelta.value - idle) : 0;
            uint64_t const busy = kernelBusy + userDelta.value;

            percentages[i] = ComputeSystemCpuPercent(busy, idle).value;
        }

        m_previousPerProcessor = perProcessor;

        // Push this sample into each processor's own ring, padding the vector out to the
        // ring count so every series keeps the length of the aggregate.
        for (size_t i = 0; i < m_perProcessorHistory.size(); ++i)
        {
            double const value = (i < percentages.size()) ? percentages[i] : 0.0;
            m_perProcessorHistory[i].Push(value);
        }

        percentages.resize(m_perProcessorHistory.size(), 0.0);
        m_perProcessorPercent = std::move(percentages);
    }

    void SystemModel::UpdatePerProcessor(std::vector<platform::ProcessorCpuTimes> const& perProcessor)
    {
        // Retained for source compatibility with tests written against the old two-call
        // shape. It deliberately does not touch the aggregate history: the correct entry
        // point is Update, which advances both in step. Calling this alongside Update is
        // what made the two series drift apart, which is what the chart grid then plotted
        // against the wrong axis.
        _appendPerProcessor(perProcessor);
        m_latest.perProcessorCpuPercent = m_perProcessorPercent;
    }

    void SystemModel::SetProcessorInfo(platform::SystemProcessorInfo info)
    {
        m_latest.processor = std::move(info);
    }

    void SystemModel::SetProcessorSpeed(platform::SystemProcessorSpeed speed)
    {
        m_latest.processorSpeed = speed;
    }

    void SystemModel::SetTotals(platform::SystemTotals totals)
    {
        m_latest.totals = totals;
    }

    void SystemModel::SetMemoryComposition(platform::SystemMemoryComposition composition)
    {
        m_latest.memoryComposition = composition;
    }

    void SystemModel::SetHardwareCounters(std::vector<platform::SystemDiskCounters> const& disks,
                                          std::vector<platform::SystemNetworkCounters> const& networks,
                                          platform::SystemGpuInfo const& gpu,
                                          uint64_t capturedAt)
    {
        double const elapsedMs = m_hasHardwareBaseline
                                     ? platform::MillisecondsBetween(m_previousHardwareCapturedAt, capturedAt)
                                     : 0.0;
        bool const canDerive = m_hasHardwareBaseline && elapsedMs > 0.0;
        double const elapsedSeconds = elapsedMs / 1000.0;

        double totalReadBps = 0.0;
        double totalWriteBps = 0.0;
        double totalActive = 0.0;
        uint32_t activeDevices = 0;

        std::vector<DiskActivity> diskActivities;
        diskActivities.reserve(disks.size());

        // Devices are matched by instance name rather than by position. The enumeration order is not
        // guaranteed to be stable between samples, and matching by index would attribute one device's
        // traffic to another the moment the order changed.
        for (platform::SystemDiskCounters const& disk : disks)
        {
            DiskActivity activity;
            activity.capacityBytes = disk.capacityBytes;
            activity.modelName = disk.modelName;
            activity.instanceName = disk.instanceName;
            activity.deviceIndex = disk.deviceIndex;
            activity.queueDepth = disk.queueDepth;
            activity.busType = disk.busType;
            activity.incursSeekPenalty = disk.incursSeekPenalty;
            activity.trimEnabled = disk.trimEnabled;
            activity.fileSystem = disk.fileSystem;
            activity.volumeLabel = disk.volumeLabel;
            activity.hostsPageFile = disk.hostsPageFile;

            auto const previous = m_previousDisks.find(disk.instanceName);
            if (canDerive && previous != m_previousDisks.end())
            {
                platform::SystemDiskCounters const& before = previous->second;

                // A counter that went backwards means the device was reset or replaced. Re-baselining
                // is the honest response; differencing would produce a negative rate or, once
                // discarded, a spike.
                bool const comparable = disk.readBytes >= before.readBytes && disk.writeBytes >= before.writeBytes &&
                                        disk.idleTimeMs >= before.idleTimeMs;

                if (comparable)
                {
                    activity.readBytesPerSecond =
                        static_cast<double>(disk.readBytes - before.readBytes) / elapsedSeconds;
                    activity.writeBytesPerSecond =
                        static_cast<double>(disk.writeBytes - before.writeBytes) / elapsedSeconds;

                    // Active time is the complement of idle time over the elapsed interval.
                    //
                    // Read service time plus write service time is not a substitute, and was the
                    // first attempt here. A device servicing overlapping requests accumulates both at
                    // once, so the sum can exceed the wall clock and reports the device as busier than
                    // it can possibly be. The idle counter is a wall-clock measure, so its complement
                    // is exactly the share of the interval the device spent working.
                    double const idleMs = static_cast<double>(disk.idleTimeMs - before.idleTimeMs);
                    activity.activePercent = std::clamp(100.0 - ((idleMs / elapsedMs) * 100.0), 0.0, 100.0);

                    // Mean response time over the interval: the service time this interval added,
                    // divided by the operations it completed. Computed from the deltas rather than
                    // differenced from the device's lifetime averages, because the difference of two
                    // means is not the mean of the period between them.
                    uint64_t const operations =
                        (disk.readCount - before.readCount) + (disk.writeCount - before.writeCount);
                    if (operations > 0)
                    {
                        double const serviceMs = static_cast<double>((disk.readTimeMs - before.readTimeMs) +
                                                                      (disk.writeTimeMs - before.writeTimeMs));
                        activity.averageResponseMs = serviceMs / static_cast<double>(operations);
                    }

                    totalReadBps += activity.readBytesPerSecond;
                    totalWriteBps += activity.writeBytesPerSecond;
                    totalActive += activity.activePercent;
                    ++activeDevices;
                }
            }

            diskActivities.push_back(std::move(activity));
        }

        // The device list is remembered whole so the next sample can difference against it.
        m_previousDisks.clear();
        for (platform::SystemDiskCounters const& disk : disks)
        {
            m_previousDisks[disk.instanceName] = disk;
        }

        double totalReceiveBps = 0.0;
        double totalSendBps = 0.0;

        std::vector<NetworkActivity> networkActivities;
        networkActivities.reserve(networks.size());

        // Interfaces are matched by name for the same reason devices are.
        for (platform::SystemNetworkCounters const& iface : networks)
        {
            NetworkActivity activity;
            activity.receiveLinkSpeedBps = iface.receiveLinkSpeedBps;
            activity.transmitLinkSpeedBps = iface.transmitLinkSpeedBps;
            activity.adapterName = iface.adapterName;
            activity.connected = iface.connected;
            activity.virtualAdapter = iface.virtualAdapter;
            activity.receiveErrors = iface.receiveErrors;
            activity.sendErrors = iface.sendErrors;
            activity.receiveDiscards = iface.receiveDiscards;
            activity.sendDiscards = iface.sendDiscards;

            auto const previous = m_previousNetworks.find(iface.adapterName);
            if (canDerive && previous != m_previousNetworks.end())
            {
                platform::SystemNetworkCounters const& before = previous->second;

                bool const comparable = iface.receivedBytes >= before.receivedBytes &&
                                        iface.sentBytes >= before.sentBytes;
                if (comparable)
                {
                    activity.receivedBytesPerSecond =
                        static_cast<double>(iface.receivedBytes - before.receivedBytes) / elapsedSeconds;
                    activity.sentBytesPerSecond =
                        static_cast<double>(iface.sentBytes - before.sentBytes) / elapsedSeconds;

                    totalReceiveBps += activity.receivedBytesPerSecond;
                    totalSendBps += activity.sentBytesPerSecond;
                }
            }

            networkActivities.push_back(std::move(activity));
        }

        m_previousNetworks.clear();
        for (platform::SystemNetworkCounters const& iface : networks)
        {
            m_previousNetworks[iface.adapterName] = iface;
        }

        // The history rings advance whether or not a rate could be derived, so every series keeps the
        // same length and the charts share one time axis.
        m_diskReadHistory.Push(canDerive ? totalReadBps : 0.0);
        m_diskWriteHistory.Push(canDerive ? totalWriteBps : 0.0);
        m_networkReceiveHistory.Push(canDerive ? totalReceiveBps : 0.0);
        m_networkSendHistory.Push(canDerive ? totalSendBps : 0.0);

        // Per-device rings, so each sidebar row shows its own trend rather than the machine's total.
        //
        // The buffer is found or emplace-constructed rather than reached through operator[]:
        // RingBuffer has no default constructor, because a buffer without a capacity would have
        // nowhere to store anything, and a map of them cannot be built empty.
        for (DiskActivity const& activity : diskActivities)
        {
            // Each direction keeps its own ring. A single summed series cannot be split back into reads
            // and writes, and the two are separately interesting, so the page needs both.
            auto const found = m_diskHistoryByDevice.find(activity.instanceName);
            SystemModel::DirectionalRings& rings =
                (found != m_diskHistoryByDevice.end())
                    ? found->second
                    : m_diskHistoryByDevice
                          .emplace(activity.instanceName, DirectionalRings{m_historyCapacity})
                          .first->second;

            rings.first.Push(activity.readBytesPerSecond);
            rings.second.Push(activity.writeBytesPerSecond);
        }

        // Every device's rings advance on every sample, including the ones absent from this sample, so
        // the per-device series stay the same length as the aggregate and share its axis. A ring that
        // missed a push would be plotted against a different window.
        for (auto& entry : m_diskHistoryByDevice)
        {
            bool const present = std::any_of(diskActivities.begin(), diskActivities.end(),
                                             [&entry](DiskActivity const& activity) {
                                                 return activity.instanceName == entry.first;
                                             });
            if (!present)
            {
                entry.second.first.Push(0.0);
                entry.second.second.Push(0.0);
            }
        }

        for (NetworkActivity const& activity : networkActivities)
        {
            auto const found = m_networkHistoryByAdapter.find(activity.adapterName);
            SystemModel::DirectionalRings& rings =
                (found != m_networkHistoryByAdapter.end())
                    ? found->second
                    : m_networkHistoryByAdapter
                          .emplace(activity.adapterName, DirectionalRings{m_historyCapacity})
                          .first->second;

            rings.first.Push(activity.receivedBytesPerSecond);
            rings.second.Push(activity.sentBytesPerSecond);
        }

        for (auto& entry : m_networkHistoryByAdapter)
        {
            bool const present = std::any_of(networkActivities.begin(), networkActivities.end(),
                                             [&entry](NetworkActivity const& activity) {
                                                 return activity.adapterName == entry.first;
                                             });
            if (!present)
            {
                entry.second.first.Push(0.0);
                entry.second.second.Push(0.0);
            }
        }

        // The GPU counters are already rates, so nothing is differenced. A reading that could not be
        // taken pushes a zero for the same reason the others do: the axis has to stay consistent.
        m_gpuHistory.Push(gpu.available ? gpu.utilizationPercent : 0.0);
        m_gpuMemoryHistory.Push(gpu.available ? static_cast<double>(gpu.dedicatedUsedBytes) : 0.0);

        // The aggregate figures are published alongside the per-device detail, so a chart can plot
        // the machine's total while the sidebar names each device.
        m_latest.disks = std::move(diskActivities);
        m_latest.networks = std::move(networkActivities);
        m_latest.gpu = gpu;
        m_latest.diskActivePercent = (activeDevices > 0) ? (totalActive / static_cast<double>(activeDevices)) : 0.0;

        m_previousHardwareCapturedAt = capturedAt;
        m_hasHardwareBaseline = true;
    }

    HistoryView SystemModel::History() const
    {
        HistoryView view;
        view.cpuTotal = m_cpuHistory.ToVector();
        view.memoryUsed = m_memoryHistory.ToVector();

        for (auto const& entry : m_diskHistoryByDevice)
        {
            HistoryView::DirectionalHistory deviceHistory;
            deviceHistory.first = entry.second.first.ToVector();
            deviceHistory.second = entry.second.second.ToVector();
            view.diskBytesPerSecondByDevice[entry.first] = std::move(deviceHistory);
        }

        for (auto const& entry : m_networkHistoryByAdapter)
        {
            HistoryView::DirectionalHistory adapterHistory;
            adapterHistory.first = entry.second.first.ToVector();
            adapterHistory.second = entry.second.second.ToVector();
            view.networkBytesPerSecondByAdapter[entry.first] = std::move(adapterHistory);
        }

        view.diskReadBytesPerSecond = m_diskReadHistory.ToVector();
        view.diskWriteBytesPerSecond = m_diskWriteHistory.ToVector();
        view.networkReceiveBytesPerSecond = m_networkReceiveHistory.ToVector();
        view.networkSendBytesPerSecond = m_networkSendHistory.ToVector();
        view.gpuUtilization = m_gpuHistory.ToVector();
        view.gpuDedicatedMemory = m_gpuMemoryHistory.ToVector();

        // The window length travels with the data, so no chart can be left without it.
        view.windowSamples = m_historyCapacity;

        view.perProcessorCpu.reserve(m_perProcessorHistory.size());
        for (auto const& ring : m_perProcessorHistory)
        {
            view.perProcessorCpu.push_back(ring.ToVector());
        }
        return view;
    }
}
