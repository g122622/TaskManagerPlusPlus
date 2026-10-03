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
    };
}
