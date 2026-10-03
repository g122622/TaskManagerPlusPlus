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
    }

    void SystemModel::Update(platform::SystemCpuTimes const& cpu,
                             platform::SystemMemoryInfo const& memory,
                             uint64_t capturedAt)
    {
        double const elapsedMs = m_hasBaseline ? platform::MillisecondsBetween(m_previousCapturedAt, capturedAt) : 0.0;

        SystemView next;
        next.version = ++m_version;
        next.capturedAt = capturedAt;
        next.elapsedMs = elapsedMs;
        next.memory = memory;
        next.processor = m_latest.processor;

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
        next.perProcessorCpuPercent = m_latest.perProcessorCpuPercent;

        // History holds placeholder values until a baseline exists, so the chart
        // keeps a consistent time axis from the first frame rather than jumping.
        m_cpuHistory.Push(cpuDerived ? next.cpuPercent : 0.0);
        m_memoryHistory.Push(next.memoryUsedPercent);

        m_latest = std::move(next);
        m_previousCpu = cpu;
        m_previousCapturedAt = capturedAt;
        m_hasBaseline = true;
    }

    void SystemModel::UpdatePerProcessor(std::vector<platform::ProcessorCpuTimes> const& perProcessor)
    {
        if (perProcessor.empty())
        {
            m_latest.perProcessorCpuPercent.clear();
            m_previousPerProcessor.clear();
            return;
        }

        std::vector<double> percentages;
        percentages.reserve(perProcessor.size());

        bool const comparable = m_previousPerProcessor.size() == perProcessor.size();
        size_t const count = std::min(perProcessor.size(), m_previousPerProcessor.size());

        for (size_t i = 0; i < perProcessor.size(); ++i)
        {
            if (!comparable || i >= count)
            {
                // Without a matching previous reading this processor has no rate yet.
                percentages.push_back(0.0);
                continue;
            }

            auto const& before = m_previousPerProcessor[i];
            auto const& now = perProcessor[i];

            Delta const kernelDelta = ComputeDelta(before.kernelTime, now.kernelTime);
            Delta const idleDelta = ComputeDelta(before.idleTime, now.idleTime);
            Delta const userDelta = ComputeDelta(before.userTime, now.userTime);

            if (!kernelDelta.Usable() || !idleDelta.Usable() || !userDelta.Usable())
            {
                percentages.push_back(0.0);
                continue;
            }

            uint64_t const idle = idleDelta.value;
            uint64_t const kernelBusy = (kernelDelta.value > idle) ? (kernelDelta.value - idle) : 0;
            uint64_t const busy = kernelBusy + userDelta.value;

            percentages.push_back(ComputeSystemCpuPercent(busy, idle).value);
        }

        m_latest.perProcessorCpuPercent = std::move(percentages);
        m_previousPerProcessor = perProcessor;
    }

    void SystemModel::SetProcessorInfo(platform::SystemProcessorInfo info)
    {
        m_latest.processor = std::move(info);
    }

    HistoryView SystemModel::History() const
    {
        HistoryView view;
        view.cpuTotal = m_cpuHistory.ToVector();
        view.memoryUsed = m_memoryHistory.ToVector();
        return view;
    }
}
