#include "ProcessModel.h"

#include <algorithm>

#include "Platform/Clock.h"

namespace tmpp::domain
{
    namespace
    {
        /**
         * @brief Builds the key used to track a process across samples.
         *
         * The creation time is what makes PID reuse safe: a recycled PID has a
         * different creation time and therefore a different key, so it starts a
         * fresh baseline instead of inheriting the old process's counters.
         */
        [[nodiscard]] uint64_t _identityKey(platform::ProcessIdentity const& identity) noexcept
        {
            // PID in the high 32 bits, the low 32 bits of the creation time below.
            // Combined, this is collision-free for any realistic process count.
            return (static_cast<uint64_t>(identity.pid) << 32) | (identity.createTime & 0xFFFFFFFFull);
        }
    }

    ProcessModel::ProcessModel(uint32_t logicalProcessorCount)
        : m_logicalProcessorCount(logicalProcessorCount == 0 ? 1 : logicalProcessorCount)
    {
        m_tracked.reserve(512);
        m_latest.processes.reserve(512);
        m_latest.logicalProcessorCount = m_logicalProcessorCount;
    }

    ProcessSnapshotView ProcessModel::Update(platform::ProcessSnapshot const& raw,
                                             SystemCpuDelta const& systemCpu,
                                             std::map<uint32_t, double> const& gpuPercentByPid)
    {
        double const elapsedMs = m_hasPreviousSample ? platform::MillisecondsBetween(m_previousCapturedAt, raw.capturedAt) : 0.0;
        uint64_t const systemDelta = systemCpu.Total();

        ProcessSnapshotView next;
        next.processes.reserve(raw.processes.size());
        next.logicalProcessorCount = m_logicalProcessorCount;
        next.capturedAt = raw.capturedAt;
        next.elapsedMs = elapsedMs;

        // A single tick snapshot of every identity seen this round, so that
        // identities that disappeared can be dropped from the tracking map.
        std::unordered_map<uint64_t, TrackedProcess> updated;
        updated.reserve(raw.processes.size());

        for (auto const& info : raw.processes)
        {
            ProcessView view;
            view.identity = info.identity;
            view.parentPid = info.parentPid;
            view.imageName = info.imageName;
            view.cpu = info.cpu;
            view.memory = info.memory;
            view.io = info.io;
            view.threadCount = info.threadCount;
            view.handleCount = info.handleCount;
            view.sessionId = info.sessionId;
            view.basePriority = info.basePriority;

            // The GPU figure comes from the performance counters rather than from the process snapshot, so
            // it is looked up by process id. A process that is not in the map is using no GPU, which is a
            // real reading and what the column shows for almost every row.
            if (auto const gpu = gpuPercentByPid.find(info.identity.pid); gpu != gpuPercentByPid.end())
            {
                view.gpuPercent = gpu->second;
            }

            uint64_t const key = _identityKey(info.identity);
            auto const previous = m_tracked.find(key);

            bool const hasBaseline = m_hasPreviousSample && previous != m_tracked.end() && elapsedMs > 0.0;

            if (hasBaseline)
            {
                auto const& before = previous->second;

                Delta const cpuKernel = ComputeDelta(before.cpu.kernelTime, info.cpu.kernelTime);
                Delta const cpuUser = ComputeDelta(before.cpu.userTime, info.cpu.userTime);
                Delta const ioRead = ComputeDelta(before.io.readTransferCount, info.io.readTransferCount);
                Delta const ioWrite = ComputeDelta(before.io.writeTransferCount, info.io.writeTransferCount);
                Delta const faults = ComputeDelta(before.pageFaults, info.memory.pageFaultCount);

                // Any rolled-back counter invalidates this process's rates for the
                // round. Reporting a partial mix of good and bogus numbers would be
                // worse than reporting none.
                bool const allUsable =
                    cpuKernel.Usable() && cpuUser.Usable() && ioRead.Usable() && ioWrite.Usable() && faults.Usable();

                if (allUsable)
                {
                    uint64_t const processDelta = cpuKernel.value + cpuUser.value;
                    view.cpuPercent = ComputeProcessCpuPercent(processDelta, systemDelta, m_logicalProcessorCount).value;

                    // Transfer counts are bytes on Windows; operation counts are not
                    // used for rates because Task Manager reports bytes.
                    view.diskReadBytesPerSec = ComputeRatePerSecond(ioRead.value, static_cast<uint64_t>(elapsedMs));
                    view.diskWriteBytesPerSec = ComputeRatePerSecond(ioWrite.value, static_cast<uint64_t>(elapsedMs));
                    view.pageFaultsPerSec = ComputeRatePerSecond(faults.value, static_cast<uint64_t>(elapsedMs));

                    view.ratesUnavailable = false;
                }
            }

            // Record the readings for the next round regardless of whether this
            // round produced rates, so a rollback re-establishes the baseline.
            TrackedProcess tracked;
            tracked.cpu = info.cpu;
            tracked.io = info.io;
            tracked.pageFaults = info.memory.pageFaultCount;
            updated.emplace(key, tracked);

            next.usedCpuPercent += view.cpuPercent;
            next.threadCount += info.threadCount;
            next.handleCount += info.handleCount;

            next.processes.push_back(std::move(view));
        }

        // Identities that vanished are no longer tracked; keeping them would let
        // the map grow without bound on a busy system.
        m_tracked = std::move(updated);

        // Resolve parent indices now so the UI never has to build the tree.
        std::unordered_map<uint32_t, uint32_t> pidToIndex;
        pidToIndex.reserve(next.processes.size());
        for (uint32_t i = 0; i < next.processes.size(); ++i)
        {
            pidToIndex.emplace(next.processes[i].identity.pid, i);
        }

        for (uint32_t i = 0; i < next.processes.size(); ++i)
        {
            ProcessView& view = next.processes[i];
            auto const parent = pidToIndex.find(view.parentPid);
            // A process may not be its own parent; some system entries report a
            // parent PID equal to their own.
            if (parent != pidToIndex.end() && parent->second != i)
            {
                view.parentIndex = parent->second;
            }
        }

        next.version = ++m_version;
        m_latest = std::move(next);
        m_previousCapturedAt = raw.capturedAt;
        m_hasPreviousSample = true;

        return m_latest;
    }
}
