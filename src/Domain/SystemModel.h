// System-wide derived state and the chart history series.
//
// This is the only place long-term numeric history is kept. The charts are
// system-level series (total CPU, memory, and later disk/network/GPU), so a
// single owner with fixed-capacity buffers keeps history memory constant no
// matter how long the application runs.
#pragma once

#include <cstdint>
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

        // Static topology, refreshed on the first sample only.
        platform::SystemProcessorInfo processor;

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
         */
        void Update(platform::SystemCpuTimes const& cpu,
                    platform::SystemMemoryInfo const& memory,
                    uint64_t capturedAt);

        /**
         * @brief Records the per-processor readings for the current sample.
         *
         * Called alongside Update when the per-processor probe succeeded. Kept
         * separate because that probe is optional and its absence must not change
         * the aggregate behaviour.
         */
        void UpdatePerProcessor(std::vector<platform::ProcessorCpuTimes> const& perProcessor);

        /**
         * @brief Supplies the static processor description, read once at startup.
         */
        void SetProcessorInfo(platform::SystemProcessorInfo info);

        [[nodiscard]] SystemView const& Latest() const noexcept { return m_latest; }

        /// Copies the charted history, oldest sample first.
        [[nodiscard]] HistoryView History() const;

        [[nodiscard]] size_t HistoryCapacity() const noexcept { return m_historyCapacity; }

    private:
        uint32_t m_logicalProcessorCount{0};
        size_t m_historyCapacity{0};

        RingBuffer<double> m_cpuHistory;
        RingBuffer<double> m_memoryHistory;

        SystemView m_latest;
        uint64_t m_version{0};

        platform::SystemCpuTimes m_previousCpu;
        std::vector<platform::ProcessorCpuTimes> m_previousPerProcessor;
        uint64_t m_previousCapturedAt{0};
        bool m_hasBaseline{false};
    };
}
