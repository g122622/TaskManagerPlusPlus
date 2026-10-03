// Per-process derived state: rates and the process tree.
//
// The model consumes raw snapshots from the Platform layer and produces
// immutable, versioned published snapshots that the UI can render without
// holding a lock or copying history.
//
// Two problems dominate this layer and both are handled here rather than at the
// call site:
//
//   1. PID reuse. Windows recycles PIDs. State is keyed on PID *plus* creation
//      time, so a recycled PID starts a fresh baseline instead of inheriting the
//      previous process's counters and reporting a huge bogus rate.
//
//   2. Counter rollback. A process that restarts, or a counter that goes
//      backwards for any reason, must not produce a negative or absurd rate.
//
// Long-term numeric history lives in SystemModel, not here: the charts are
// system-level series. This model is about "what is true right now".
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Domain/RateMath.h"
#include "Domain/SamplingConfig.h"
#include "Platform/Windows/WindowsProcessProbe.h"

namespace tmpp::domain
{
    /// Index value meaning "no such process".
    inline constexpr uint32_t NO_PARENT_INDEX = 0xFFFFFFFFu;

    /**
     * @brief System-wide CPU time consumed over the sampling interval.
     *
     * Supplied by the caller because it comes from a different probe than the
     * per-process counters, and because passing it explicitly makes the
     * dependency visible instead of hiding it in mutable state.
     */
    struct SystemCpuDelta
    {
        uint64_t busy{0}; ///< Kernel-excluding-idle plus user, in 100 ns ticks.
        uint64_t idle{0}; ///< Idle time over the same interval, same units.

        [[nodiscard]] uint64_t Total() const noexcept { return busy + idle; }
    };

    /**
     * @brief A process's state as published to the UI.
     *
     * Carries both raw cumulative counters and derived rates. The raw values are
     * kept so the details view can show totals and so the next sample can
     * difference against them.
     */
    struct ProcessView
    {
        platform::ProcessIdentity identity;
        uint32_t parentPid{0};

        /// Raw image name from the snapshot. Empty for the idle process, which has
        /// no executable image; presenting that as a name is the UI's decision.
        std::string imageName;

        // Derived over the sampling interval.
        double cpuPercent{0.0};
        double diskReadBytesPerSec{0.0};
        double diskWriteBytesPerSec{0.0};
        double pageFaultsPerSec{0.0};

        // Cumulative readings, passed through for the details view.
        platform::ProcessCpuTimes cpu;
        platform::ProcessMemoryCounters memory;
        platform::ProcessIoCounters io;
        uint32_t threadCount{0};
        uint32_t handleCount{0};
        uint32_t sessionId{0};
        int32_t basePriority{0};

        /// Index of the parent within the same snapshot, or NO_PARENT_INDEX when
        /// the parent is absent. Precomputed during sampling: the UI never builds
        /// the tree.
        uint32_t parentIndex{NO_PARENT_INDEX};

        /// True when no baseline existed yet, or the counters rolled back, so the
        /// rates above are not meaningful. The UI shows a blank rather than 0.
        bool ratesUnavailable{true};
    };

    /**
     * @brief A process snapshot published to the UI.
     *
     * Immutable once published; a new instance replaces it on each Update. The
     * version increments on every publication so a render pass can detect change
     * without comparing contents.
     */
    struct ProcessSnapshotView
    {
        std::vector<ProcessView> processes;
        uint64_t version{0};
        uint64_t capturedAt{0};
        uint32_t logicalProcessorCount{0};
        double elapsedMs{0.0};

        /// Aggregates the UI shows in headers.
        double usedCpuPercent{0.0};
        size_t threadCount{0};
        size_t handleCount{0};
    };

    /**
     * @brief Derives per-process rates and the process tree from raw snapshots.
     *
     * Not thread-safe: the owner calls Update from a single sampling thread and
     * publishes the returned snapshot.
     */
    class ProcessModel
    {
    public:
        /**
         * @param logicalProcessorCount Scales per-process CPU percentages.
         */
        explicit ProcessModel(uint32_t logicalProcessorCount);

        /**
         * @brief Consumes a raw snapshot and publishes a derived view.
         *
         * On the first call there is no baseline, so every process reports
         * ratesUnavailable and zero rates. Subsequent calls difference against the
         * previous readings for the same identity.
         *
         * @param raw Current raw snapshot.
         * @param systemCpu System-wide CPU delta covering the same interval.
         * @return The newly published snapshot.
         */
        ProcessSnapshotView Update(platform::ProcessSnapshot const& raw, SystemCpuDelta const& systemCpu);

        [[nodiscard]] ProcessSnapshotView const& Latest() const noexcept { return m_latest; }

        /// Number of distinct process identities currently tracked.
        [[nodiscard]] size_t TrackedCount() const noexcept { return m_tracked.size(); }

    private:
        /**
         * @brief Per-identity counters carried between samples.
         *
         * Keyed on identity rather than PID so a recycled PID does not inherit
         * these values.
         */
        struct TrackedProcess
        {
            platform::ProcessCpuTimes cpu;
            platform::ProcessIoCounters io;
            uint32_t pageFaults{0};
        };

        uint32_t m_logicalProcessorCount{0};
        std::unordered_map<uint64_t, TrackedProcess> m_tracked;
        ProcessSnapshotView m_latest;
        uint64_t m_version{0};
        uint64_t m_previousCapturedAt{0};
        bool m_hasPreviousSample{false};
    };
}
