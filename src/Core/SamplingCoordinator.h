// Wires the probes, the models and the sampler together.
//
// This is the composition root: the only place that constructs the platform
// probes and injects them into the Domain models, and the only owner of the
// sampling thread. Keeping it in Core rather than in a UI panel means the UI
// never needs to know which probe implementation is in use.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "Core/BackgroundSampler.h"
#include "Core/Settings.h"
#include "Domain/ProcessModel.h"
#include "Domain/SystemModel.h"
#include "Platform/Windows/WindowsProcessProbe.h"
#include "Platform/Windows/WindowsSystemProbe.h"

namespace tmpp::core
{
    /**
     * @brief Status of the most recent sampling attempt.
     *
     * Surfaced to the UI so that a probe failure is visible rather than silently
     * producing stale or empty charts.
     */
    struct SamplingStatus
    {
        bool processEnumerationSucceeded{false};
        bool cpuReadSucceeded{false};
        bool memoryReadSucceeded{false};
        bool perProcessorReadSucceeded{false};

        /// Consecutive failures, used to throttle logging in the caller.
        uint32_t consecutiveFailures{0};
    };

    /**
     * @brief Owns the probes, the models and the sampling thread.
     *
     * Threading: sampling runs on the sampler thread and mutates the models. The
     * UI thread reads published snapshots. Both access the same model objects, so
     * a mutex guards them and the UI takes it only to copy the current
     * publication, never while rendering.
     */
    class SamplingCoordinator
    {
    public:
        /**
         * @brief Constructs the coordinator and resolves static machine info.
         *
         * Performs a one-off read of the processor topology and capabilities. Does
         * not start sampling.
         *
         * @param initialIntervalMs Clamped to the permitted range.
         */
        explicit SamplingCoordinator(uint32_t initialIntervalMs);

        ~SamplingCoordinator();

        SamplingCoordinator(SamplingCoordinator const&) = delete;
        SamplingCoordinator& operator=(SamplingCoordinator const&) = delete;

        /// Starts the sampling thread and takes the first sample immediately.
        void Start();

        /// Stops sampling and joins the thread.
        void Stop();

        /// Requests an out-of-band sample.
        void RequestRefresh();

        /**
         * @brief Changes the sampling interval, applying it immediately.
         */
        void SetInterval(uint32_t intervalMs);

        /**
         * @brief Applies the minimised-window policy and updates the interval.
         *
         * Reducing the cadence while minimised is what keeps the application from
         * consuming a noticeable amount of CPU when it is not visible.
         */
        void SetMinimized(bool minimized);

        /**
         * @brief Returns the version of the newest published process snapshot.
         *
         * The UI polls this to decide whether to copy. Comparing a version is
         * essentially free, whereas copying the snapshot is a deep copy of every
         * process -- on a busy system that is hundreds of strings. Polling the
         * version and copying only on change is what keeps the render loop off the
         * sampling lock (docs/ARCHITECTURE.md, constraint P-004).
         */
        [[nodiscard]] uint64_t ProcessVersion() const noexcept;

        /**
         * @brief Returns the version of the newest published system snapshot.
         */
        [[nodiscard]] uint64_t SystemVersion() const noexcept;

        /**
         * @brief Copies the current process snapshot.
         *
         * The returned value is a copy taken under the lock, so the caller may
         * render it freely. Callers should check ProcessVersion() first and skip
         * this call when it is unchanged.
         */
        [[nodiscard]] domain::ProcessSnapshotView CurrentProcesses() const;

        /**
         * @brief Copies the current system state and chart history.
         */
        [[nodiscard]] domain::SystemView CurrentSystem() const;

        /**
         * @brief Copies the charted history, oldest sample first.
         */
        [[nodiscard]] domain::HistoryView CurrentHistory() const;

        [[nodiscard]] SamplingStatus Status() const;

        [[nodiscard]] platform::ProcessCapabilities ProcessCapabilities() const noexcept
        {
            return m_processProbe.Capabilities();
        }

        [[nodiscard]] platform::SystemCapabilities SystemCapabilities() const noexcept
        {
            return m_systemProbe.Capabilities();
        }

        [[nodiscard]] uint32_t LogicalProcessorCount() const noexcept { return m_logicalProcessorCount; }

        [[nodiscard]] uint32_t IntervalMs() const noexcept { return m_sampler.IntervalMs(); }

        /// Samples taken since construction, for the status bar.
        [[nodiscard]] uint64_t SampleCount() const noexcept { return m_sampler.SampleCount(); }

    private:
        /// Runs one full sample. Called on the sampler thread.
        void _sample(WakeReason reason);

        /// Reads everything the models need and updates them. Returns the status.
        SamplingStatus _collect();

        mutable std::mutex m_mutex;

        platform::WindowsProcessProbe m_processProbe;
        platform::WindowsSystemProbe m_systemProbe;

        domain::ProcessModel m_processModel;
        domain::SystemModel m_systemModel;

        BackgroundSampler m_sampler;

        uint32_t m_logicalProcessorCount{1};
        Settings m_settings;

        /// Previous cumulative system CPU reading, kept to build the delta the
        /// process model needs in order to express a process's share of the system.
        platform::SystemCpuTimes m_previousCpu;
        bool m_hasCpuBaseline{false};

        SamplingStatus m_status;
    };
}
