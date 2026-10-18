#include "Core/SamplingCoordinator.h"

#include "Platform/Clock.h"

#include "Platform/Windows/HardwareCounterProbe.h"
#include "Platform/Windows/SmbiosMemoryProbe.h"

#include "Core/Logging.h"

#include <algorithm>

namespace tmpp::core
{
    namespace
    {
        /// Repeated identical failures are logged only every Nth time, so a
        /// persistently unavailable metric cannot flood the log from the sampling
        /// thread. The first failure is always logged.
        constexpr uint32_t LOG_EVERY_N_FAILURES = 30;

        /**
         * @brief Static machine description, read once per process.
         *
         * The models must be sized in this class's initialiser list, before its own
         * probe member exists, and the topology never changes while the process
         * runs. Caching it here means the expensive query happens exactly once no
         * matter how many callers need it.
         */
        struct CachedProcessorInfo
        {
            platform::SystemProcessorInfo info;
            uint32_t logicalProcessorCount{1};
        };

        [[nodiscard]] CachedProcessorInfo const& _processorInfo() noexcept
        {
            static CachedProcessorInfo const cached = []() noexcept -> CachedProcessorInfo {
                platform::WindowsSystemProbe probe;
                auto const result = probe.ReadProcessorInfo();

                CachedProcessorInfo value;
                if (result.Success())
                {
                    value.info = result.Value();
                    value.logicalProcessorCount = std::max(1u, value.info.logicalProcessorCount);
                }
                else
                {
                    // Not fatal: the application still runs, it just cannot scale CPU
                    // percentages correctly, so a single processor is assumed.
                    spdlog::warn("Could not read processor topology ({}); assuming 1 logical processor",
                                 result.GetError().Message());
                    value.logicalProcessorCount = 1;
                }
                return value;
            }();
            return cached;
        }
    }

    SamplingCoordinator::SamplingCoordinator(uint32_t initialIntervalMs, std::string processRulesFile)
        : m_processModel(_processorInfo().logicalProcessorCount),
          m_systemModel(_processorInfo().logicalProcessorCount,
                        domain::sampling::ClampInterval(initialIntervalMs),
                        domain::sampling::DEFAULT_HISTORY_SECONDS),
          m_sampler(initialIntervalMs, [this](WakeReason reason) { _sample(reason); })
    {
        m_logicalProcessorCount = _processorInfo().logicalProcessorCount;
        m_systemModel.SetProcessorInfo(_processorInfo().info);

        // The live speed counter is a percentage of the rated clock, so the probe
        // cannot be built before the topology has been read.
        m_hardwareProbe = std::make_unique<platform::HardwareCounterProbe>();

        // Read once: the modules a machine has do not change while it runs, so this is a startup cost
        // rather than a per-sample one.
        if (auto const slots = platform::SmbiosMemoryProbe{}.Read(); slots.Success())
        {
            m_memorySlots = slots.Value();
        }

        // Read once for the same reason: which logical processor is a thread of which core cannot
        // change while the machine runs, and the relationship walk is not cheap enough to repeat.
        {
            platform::WindowsSystemProbe probe;
            if (auto const topology = probe.ReadProcessorTopology(); topology.Success())
            {
                m_processorTopology = topology.Value();
                spdlog::info("Processor topology: {} logical processors on {} physical cores in {} group(s){}",
                             m_processorTopology.logicalProcessors.size(),
                             m_processorTopology.physicalCoreCount,
                             m_processorTopology.groupCount,
                             m_processorTopology.hybrid ? ", hybrid" : "");
            }
            else
            {
                // Not fatal: the affinity editor falls back on the plain logical count and loses only
                // the physical-core grouping, which is a presentation detail rather than a
                // correctness one.
                spdlog::warn("Could not read the processor core topology ({}); CPU cores will be "
                             "listed without their physical grouping",
                             topology.GetError().Message());
            }
        }

        // Rules are an accelerator for a repeated workflow, so an unreadable file costs the user
        // their rules and nothing else. A path is only supplied by the application; a test leaves it
        // empty, and no file is then touched.
        if (!processRulesFile.empty())
        {
            m_ruleStore = std::make_unique<ProcessRuleStore>(processRulesFile);
            m_ruleEngine.SetRules(m_ruleStore->Load());
            spdlog::info("Loaded {} process rule(s) from {}", m_ruleEngine.Rules().size(), processRulesFile);
        }

        m_speedProbe = std::make_unique<platform::ProcessorSpeedProbe>(_processorInfo().info.baseClockMhz);
        if (!m_speedProbe->Available())
        {
            spdlog::info("Processor speed counter unavailable; the speed readout will be blank");
        }
    }

    SamplingCoordinator::~SamplingCoordinator()
    {
        Stop();
    }

    void SamplingCoordinator::Start()
    {
        spdlog::info("Sampling started: {} logical processors, {} ms interval",
                     m_logicalProcessorCount,
                     m_sampler.IntervalMs());
        m_sampler.Start();
    }

    void SamplingCoordinator::Stop()
    {
        m_sampler.Stop();
    }

    void SamplingCoordinator::RequestRefresh()
    {
        m_sampler.RequestRefresh();
    }

    void SamplingCoordinator::SetInterval(uint32_t intervalMs)
    {
        m_sampler.SetInterval(intervalMs);

        // The history capacity is derived from the interval, so the buffers are
        // rebuilt to keep the retained window the same length in wall-clock terms.
        // TODO: this discards the history that was already collected, so the charts
        //       briefly restart. Preserving it would need the ring buffer to be
        //       resampled rather than replaced.
        std::lock_guard const lock(m_mutex);
        m_systemModel = domain::SystemModel(m_logicalProcessorCount,
                                            domain::sampling::ClampInterval(intervalMs),
                                            m_settings.HistorySeconds());
    }

    void SamplingCoordinator::SetMinimized(bool minimized)
    {
        if (m_settings.reduceWhenMinimized)
        {
            m_sampler.SetInterval(m_settings.EffectiveIntervalMs(minimized));
        }
    }

    platform::ProcessActionResult SamplingCoordinator::TerminateProcess(uint32_t pid, bool entireTree)
    {
        // The tree comes from a fresh copy of the latest snapshot rather than from the caller, so the
        // parent links and the process list are guaranteed to be from the same sample. A caller passing
        // its own view would be passing links that could already be stale.
        std::vector<std::pair<uint32_t, uint32_t>> parentByPid;

        // The creation time of the process the user pointed at. It is taken from the same snapshot the
        // tree is, and it is what stops a reused pid from being terminated: between the list being drawn
        // and the menu item being clicked, the target may have exited and its identifier been given to
        // something else. Without this the action would end an unrelated process.
        uint64_t createTime = 0;

        {
            domain::ProcessSnapshotView const snapshot = CurrentProcesses();

            for (domain::ProcessView const& process : snapshot.processes)
            {
                if (process.identity.pid == pid)
                {
                    createTime = process.identity.createTime;
                }

                // Only a parent that is itself in the snapshot is recorded. A parent that has exited has
                // no pid to end, and recording it would walk into whatever now holds its id.
                if (entireTree && process.parentPid != 0 && process.parentPid != process.identity.pid)
                {
                    parentByPid.emplace_back(process.identity.pid, process.parentPid);
                }
            }
        }

        platform::ProcessActionResult const result =
            entireTree ? m_processActions.TerminateTree(pid, createTime, parentByPid)
                       : m_processActions.Terminate(pid, createTime);

        // A process that has just ended is reflected immediately rather than at the next tick, so the
        // list does not appear to have ignored the request.
        if (result.affected > 0)
        {
            RequestRefresh();
        }

        return result;
    }

    // ========================================================================
    // Process tuning
    // ========================================================================

    platform::ProcessTuningState SamplingCoordinator::ReadProcessTuning(uint32_t pid) const
    {
        // The platform read happens outside the lock. It opens a process handle, which can block on a
        // process that is being torn down, and holding the sampling mutex across it would stall the
        // sampler for as long as that took.
        platform::ProcessTuningState state;
        if (auto const read = m_processTuning.Read(pid); read.Success())
        {
            state = read.Value();
        }

        // The suspend state comes from the snapshot rather than from the platform module, which
        // reports it as unknown: the kernel offers no query for it, and the bulk snapshot -- where
        // every thread's wait reason is already in hand -- is the only place it can be answered
        // without attempting a modification and observing whether it is refused.
        {
            std::lock_guard const lock(m_mutex);
            for (domain::ProcessView const& process : m_processModel.Latest().processes)
            {
                if (process.identity.pid == pid)
                {
                    state.suspendState = process.suspended ? platform::ProcessSuspendState::Suspended
                                                          : platform::ProcessSuspendState::Running;
                    break;
                }
            }
        }

        return state;
    }

    platform::ProcessActionResult SamplingCoordinator::ApplyProcessTuning(uint32_t pid,
                                                                         platform::ProcessTuningChange const& change)
    {
        // The creation time is taken from the snapshot rather than supplied by the caller, for the
        // same reason TerminateProcess takes its tree from one: a list drawn a second ago may name a
        // process that has since exited and had its identifier handed to something else.
        uint64_t createTime = 0;
        {
            std::lock_guard const lock(m_mutex);
            for (domain::ProcessView const& process : m_processModel.Latest().processes)
            {
                if (process.identity.pid == pid)
                {
                    createTime = process.identity.createTime;
                    break;
                }
            }
        }

        platform::ProcessActionResult const result = m_processTuning.Apply(pid, createTime, change);

        // A priority or a suspension is not reflected in the counters until the next round, so the
        // list would otherwise appear to have ignored the request.
        if (result.applied > 0)
        {
            RequestRefresh();
        }

        return result;
    }

    std::string SamplingCoordinator::ProcessExecutablePath(uint32_t pid) const
    {
        return m_processTuning.ExecutablePath(pid);
    }

    std::vector<ProcessRule> SamplingCoordinator::ProcessRules() const
    {
        return m_ruleEngine.Rules();
    }

    void SamplingCoordinator::SetProcessRules(std::vector<ProcessRule> rules)
    {
        if (m_ruleStore != nullptr)
        {
            if (auto const saved = m_ruleStore->Save(rules); !saved.Success())
            {
                spdlog::warn("Process rules could not be written to {}: {}",
                             m_ruleStore->FilePath(),
                             saved.GetError().Message());
            }
        }

        m_ruleEngine.SetRules(std::move(rules));
    }

    std::vector<ProcessRuleEvent> SamplingCoordinator::TakeProcessRuleEvents()
    {
        return m_ruleEngine.TakeEvents();
    }

    void SamplingCoordinator::_sample(WakeReason reason)
    {
        // A manual refresh is sampled exactly like a scheduled tick; the reason is
        // recorded only so a caller could distinguish them for diagnostics.
        (void)reason;

        SamplingStatus const status = _collect();

        std::lock_guard const lock(m_mutex);
        m_status = status;
    }

    SamplingStatus SamplingCoordinator::_collect()
    {
        SamplingStatus status;

        platform::Timestamp const now = platform::SteadyNow();

        // --- Memory is instantaneous, so it never depends on a previous sample and
        // is read first.
        auto const memory = m_systemProbe.ReadMemoryInfo();
        status.memoryReadSucceeded = memory.Success();

        // --- CPU: read the new cumulative values and difference against the previous
        // reading. That delta is what both models need.
        auto const cpu = m_systemProbe.ReadCpuTimes();
        status.cpuReadSucceeded = cpu.Success();

        domain::SystemCpuDelta processCpuDelta;
        bool haveCpuDelta = false;

        if (cpu.Success())
        {
            if (m_hasCpuBaseline)
            {
                // Kernel time includes idle, so each field is differenced separately;
                // differencing the totals first would double-count idle.
                domain::Delta const kernelDelta =
                    domain::ComputeDelta(m_previousCpu.kernelTime, cpu.Value().kernelTime);
                domain::Delta const idleDelta = domain::ComputeDelta(m_previousCpu.idleTime, cpu.Value().idleTime);
                domain::Delta const userDelta = domain::ComputeDelta(m_previousCpu.userTime, cpu.Value().userTime);

                if (kernelDelta.Usable() && idleDelta.Usable() && userDelta.Usable())
                {
                    uint64_t const idle = idleDelta.value;
                    uint64_t const kernelBusy = (kernelDelta.value > idle) ? (kernelDelta.value - idle) : 0;
                    processCpuDelta.busy = kernelBusy + userDelta.value;
                    processCpuDelta.idle = idle;
                    haveCpuDelta = true;
                }
            }

            m_previousCpu = cpu.Value();
            m_hasCpuBaseline = true;
        }

        // --- Per-processor CPU is optional; its absence must not affect the totals.
        //
        // Read before the publish below so the whole sample -- aggregate and per-core --
        // is recorded by one Update call. When these were separate calls a failed probe
        // skipped its push and left the per-core series longer than the aggregate.
        auto const perProcessor = m_systemProbe.ReadPerProcessorCpuTimes();
        status.perProcessorReadSucceeded = perProcessor.Success();

        // --- Publish memory, CPU and per-processor together so every series advances once.
        {
            std::lock_guard const lock(m_mutex);

            platform::SystemMemoryInfo memoryInfo;
            if (memory.Success())
            {
                memoryInfo = memory.Value();
            }

            static std::vector<platform::ProcessorCpuTimes> const noPerProcessor;

            // On a CPU read failure the previous reading is re-published rather than a
            // zeroed one, so the model records no elapsed progress instead of a fabricated
            // idle period.
            m_systemModel.Update(cpu.Success() ? cpu.Value() : m_previousCpu,
                                 memoryInfo,
                                 now,
                                 perProcessor.Success() ? perProcessor.Value() : noPerProcessor);
        }

        // --- GPU, read before the process list so its per-process figures land in the same sample ----
        //
        // The GPU counters are the only source of a per-process GPU figure, so they have to be collected
        // before the process model is updated. Reading them here rather than with the disks and network
        // below is what lets one sample carry both the adapter's total and each process's share of it.
        //
        // The engine counters are collected once by ReadGpu, and ReadProcessGpu then reads the same
        // collected values; collecting twice within one sample would measure an interval of nearly zero
        // for whichever reading came second.
        std::map<uint32_t, double> processGpu;
        if (m_hardwareProbe != nullptr)
        {
            if (auto const read = m_hardwareProbe->ReadGpu(); read.Success() && read.Value().available)
            {
                m_lastGpu = read.Value();
                m_hasLastGpu = true;
            }

            m_hardwareProbe->ReadProcessGpu(processGpu);
        }

        // --- Processes: one bulk snapshot for the whole system.
        auto const processes = m_processProbe.Enumerate();
        status.processEnumerationSucceeded = processes.Success();

        uint32_t threadTotal = 0;
        uint32_t handleTotal = 0;
        uint32_t processTotal = 0;

        // Kept rather than discarded, because the rule engine is evaluated against it below. The
        // model returns the snapshot it just published, so this is a pointer's worth of work rather
        // than a second copy of the process list.
        domain::ProcessSnapshotView published;

        if (processes.Success())
        {
            // The thread and handle counts come from the snapshot already in hand, so
            // the totals cost nothing beyond this summation. Re-walking the process
            // list for them would double the cost of every sample.
            for (auto const& process : processes.Value().processes)
            {
                threadTotal += process.threadCount;
                handleTotal += process.handleCount;
            }
            processTotal = static_cast<uint32_t>(processes.Value().processes.size());

            std::lock_guard const lock(m_mutex);
            published = m_processModel.Update(processes.Value(),
                                              haveCpuDelta ? processCpuDelta : domain::SystemCpuDelta{},
                                              processGpu);
        }

        // --- Live clock speed. Independent of everything above: a missing counter must
        // not disturb the other readings.
        if (m_speedProbe != nullptr)
        {
            auto const speed = m_speedProbe->Read();
            status.processorSpeedReadSucceeded = speed.Success();

            if (speed.Success())
            {
                std::lock_guard const lock(m_mutex);
                m_systemModel.SetProcessorSpeed(speed.Value());
            }
        }

        // --- Memory composition. Independent of everything above: a failure here hides one
        // strip rather than affecting the memory figures.
        if (auto const composition = m_systemProbe.ReadMemoryComposition(); composition.Success())
        {
            platform::SystemMemoryComposition value = composition.Value();

            // The compressed figure is taken from the process snapshot already in hand rather than
            // from a query of its own: the compression store is the residency of the process Windows
            // dedicates to it, and a second bulk enumeration for one number would double the cost of
            // the round (docs/METRICS.md, P-001). A round whose process enumeration failed reports
            // zero, which the strip draws as no hatch rather than as a stale length.
            value.compressedBytes = processes.Success()
                                        ? platform::WindowsProcessProbe::CompressedMemoryBytes(processes.Value())
                                        : 0;

            std::lock_guard const lock(m_mutex);
            m_systemModel.SetMemoryComposition(value);
        }

        // --- Rolling totals.
        if (auto const totals = m_systemProbe.ReadTotals(processTotal, threadTotal, handleTotal); totals.Success())
        {
            std::lock_guard const lock(m_mutex);
            m_systemModel.SetTotals(totals.Value());
        }

        // --- Disk, network and GPU.
        //
        // Collected together because they share one interval: the disk and network rates are derived
        // by differencing against the previous round, so they must be told the same timestamp the CPU
        // and memory figures were given. Passing a separately-read clock here would put these series
        // on a slightly different interval from the rest of the page.
        //
        // Each source is independent: a machine without GPU counters still reports its disks, and a
        // failure in one must not discard the others.
        if (m_hardwareProbe != nullptr)
        {
            std::vector<platform::SystemDiskCounters> disks;
            std::vector<platform::SystemNetworkCounters> networks;
            platform::SystemGpuInfo gpu;

            // A failed read leaves the previous device list in place rather than replacing it with
            // nothing. The sidebar builds one row per device, so an empty sample does not merely show a
            // gap in the figures: every disk and network row disappears and comes back, which is what a
            // user sees as the items flickering out of the list.
            //
            // The previous list is held here rather than in the model because it is exactly the value
            // the model still holds, and the model has no way to say "unchanged" through a call whose
            // whole purpose is to publish a new sample.
            // A read is only accepted when it reported at least one device. A successful read that
            // returns nothing is a failure to observe rather than an observation that the hardware is
            // gone: the devices are physically present, and each one is skipped when it cannot be
            // opened, so a transient failure across all of them produces an empty list that still
            // reports success.
            //
            // Accepting that empty list was the cause of the sidebar showing "unavailable" against every
            // disk: the empty result was cached as the last good reading, and every later sample
            // republished it.
            if (auto const read = m_hardwareProbe->ReadDisks();
                read.Success() && !read.Value().empty())
            {
                disks = read.Value();
                status.disksAvailable = m_hardwareProbe->DisksAvailable();
                m_lastDisks = disks;
            }
            else
            {
                disks = m_lastDisks;
            }

            if (auto const read = m_hardwareProbe->ReadNetwork();
                read.Success() && !read.Value().empty())
            {
                networks = read.Value();
                status.networksAvailable = m_hardwareProbe->NetworkAvailable();
                m_lastNetworks = networks;
            }
            else
            {
                networks = m_lastNetworks;
            }

            // The GPU was already read above, before the process list, so its per-process figures land in
            // the same sample. This block only publishes it, using the reading taken there.
            if (m_hasLastGpu)
            {
                // The GPU counters are rates rather than cumulative totals, so a stale reading is a
                // reading that was true a moment ago and is still the best available. Reporting the
                // cached one keeps the row populated without claiming a fresh measurement.
                gpu = m_lastGpu;
                status.gpuAvailable = m_hardwareProbe->GpuAvailable();
            }

            std::lock_guard const lock(m_mutex);
            m_systemModel.SetHardwareCounters(disks, networks, gpu, now);
        }

        // --- Failure accounting. Only failures are counted; a fully successful round
        // clears the streak so the next failure after a healthy period logs at once.
        bool const anyFailure = !status.processEnumerationSucceeded || !status.cpuReadSucceeded ||
                                !status.memoryReadSucceeded;
        if (anyFailure)
        {
            std::lock_guard const lock(m_mutex);
            status.consecutiveFailures = m_status.consecutiveFailures + 1;

            if (status.consecutiveFailures == 1 || status.consecutiveFailures % LOG_EVERY_N_FAILURES == 1)
            {
                spdlog::warn("Sampling failure #{}: processes={} cpu={} memory={} perProcessor={}",
                             status.consecutiveFailures,
                             status.processEnumerationSucceeded,
                             status.cpuReadSucceeded,
                             status.memoryReadSucceeded,
                             status.perProcessorReadSucceeded);
            }
        }

        // --- Saved rules, last -------------------------------------------------
        //
        // Last because a rule that adjusts a process should not be able to disturb the round it was
        // observed in: everything above reads a snapshot that is already taken, and the change shows
        // up in the next one. Evaluated on this thread rather than the UI's because this is where a
        // newly appeared process is first seen, and because opening a handle per new process on the
        // UI thread would put an unbounded cost on a frame.
        //
        // Skipped entirely when no rule is enabled, which is the state of a machine whose user has
        // never used the feature: the check is a size comparison and costs nothing per sample.
        if (!published.processes.empty() && m_ruleEngine.HasActiveRules())
        {
            m_ruleEngine.Evaluate(published, [this](uint32_t pid,
                                                    uint64_t createTime,
                                                    platform::ProcessTuningChange const& change) {
                return m_processTuning.Apply(pid, createTime, change);
            });
        }

        return status;
    }

    uint64_t SamplingCoordinator::ProcessVersion() const noexcept
    {
        // A lock is still needed, but it is held for a word read rather than a deep
        // copy of every process.
        std::lock_guard const lock(m_mutex);
        return m_processModel.Latest().version;
    }

    uint64_t SamplingCoordinator::SystemVersion() const noexcept
    {
        std::lock_guard const lock(m_mutex);
        return m_systemModel.Latest().version;
    }

    domain::ProcessSnapshotView SamplingCoordinator::CurrentProcesses() const
    {
        std::lock_guard const lock(m_mutex);
        return m_processModel.Latest();
    }

    domain::SystemView SamplingCoordinator::CurrentSystem() const
    {
        std::lock_guard const lock(m_mutex);
        return m_systemModel.Latest();
    }

    domain::HistoryView SamplingCoordinator::CurrentHistory() const
    {
        std::lock_guard const lock(m_mutex);
        return m_systemModel.History();
    }

    SamplingStatus SamplingCoordinator::Status() const
    {
        std::lock_guard const lock(m_mutex);
        return m_status;
    }
}
