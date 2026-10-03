#include "Core/SamplingCoordinator.h"

#include "Platform/Clock.h"

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

    SamplingCoordinator::SamplingCoordinator(uint32_t initialIntervalMs)
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

        // --- Processes: one bulk snapshot for the whole system.
        auto const processes = m_processProbe.Enumerate();
        status.processEnumerationSucceeded = processes.Success();

        uint32_t threadTotal = 0;
        uint32_t handleTotal = 0;
        uint32_t processTotal = 0;

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
            m_processModel.Update(processes.Value(), haveCpuDelta ? processCpuDelta : domain::SystemCpuDelta{});
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

        // --- Rolling totals.
        if (auto const totals = m_systemProbe.ReadTotals(processTotal, threadTotal, handleTotal); totals.Success())
        {
            std::lock_guard const lock(m_mutex);
            m_systemModel.SetTotals(totals.Value());
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
