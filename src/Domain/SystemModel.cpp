#include "SystemModel.h"

#include <algorithm>

#include "Platform/Clock.h"

namespace tmpp::domain
{
    SystemModel::SystemModel(uint32_t logicalProcessorCount, uint32_t intervalMs, uint32_t historySeconds)
        : m_logicalProcessorCount(logicalProcessorCount == 0 ? 1 : logicalProcessorCount),
          m_historyCapacity(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_cpuHistory(sampling::HistoryCapacity(intervalMs, historySeconds)),
          m_memoryHistory(sampling::HistoryCapacity(intervalMs, historySeconds))
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

    HistoryView SystemModel::History() const
    {
        HistoryView view;
        view.cpuTotal = m_cpuHistory.ToVector();
        view.memoryUsed = m_memoryHistory.ToVector();

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
