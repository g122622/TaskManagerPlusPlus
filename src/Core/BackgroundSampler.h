// Background sampling loop.
//
// Sampling must never block the UI thread. Heavy work (enumerating thousands of
// processes, PDH queries, GPU counters) is done here, on one dedicated thread,
// which publishes results through a callback.
//
// The loop must also respond to changes immediately rather than sleeping out the
// current interval: changing the interval, requesting a refresh, and stopping all
// wake it at once. Sleeping for the whole interval would make the application feel
// unresponsive exactly when the user is interacting with it.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

#include "Domain/SamplingConfig.h"

namespace tmpp::core
{
    /**
     * @brief Why the sampler woke up.
     *
     * The callback can distinguish a scheduled tick from a user-initiated refresh,
     * which matters because a manual refresh should not be counted as a normal
     * sampling interval for rate purposes if it arrives very soon after the last.
     */
    enum class WakeReason
    {
        Initial,  ///< The first sample, taken immediately on start.
        Interval, ///< The configured interval elapsed.
        Refresh,  ///< Refresh was requested explicitly.
        IntervalChanged,
    };

    /**
     * @brief Runs a sampling callback on a dedicated thread at a fixed cadence.
     *
     * The callback runs on the sampler thread, so it must not touch UI objects.
     * It should do its work and publish a result; anything else belongs elsewhere.
     */
    class BackgroundSampler
    {
    public:
        /// The work to perform on each wake-up.
        using SampleCallback = std::function<void(WakeReason)>;

        /**
         * @brief Constructs a sampler; call Start to begin sampling.
         *
         * @param intervalMs Requested interval. Clamped to the permitted range.
         * @param callback Invoked on the sampler thread.
         */
        BackgroundSampler(uint32_t intervalMs, SampleCallback callback);

        /// Requests stop and joins the thread. Safe to call more than once.
        ~BackgroundSampler();

        BackgroundSampler(BackgroundSampler const&) = delete;
        BackgroundSampler& operator=(BackgroundSampler const&) = delete;
        BackgroundSampler(BackgroundSampler&&) = delete;
        BackgroundSampler& operator=(BackgroundSampler&&) = delete;

        /**
         * @brief Starts the sampling thread and takes a first sample immediately.
         *
         * Sampling immediately means the UI has data on its first frame rather
         * than showing empty charts for a full interval.
         */
        void Start();

        /// Requests stop and joins. Idempotent.
        void Stop();

        /**
         * @brief Wakes the sampler to sample right away.
         *
         * Used for a manual refresh and after a settings change.
         */
        void RequestRefresh();

        /**
         * @brief Changes the sampling interval and wakes the sampler.
         *
         * The new interval takes effect immediately rather than after the old one
         * expires, so moving the slider to "5 seconds" visibly slows sampling at
         * once instead of up to five seconds later.
         */
        void SetInterval(uint32_t intervalMs);

        [[nodiscard]] uint32_t IntervalMs() const noexcept { return m_intervalMs.load(std::memory_order_relaxed); }

        /// Number of completed samples. Used by tests and diagnostics.
        [[nodiscard]] uint64_t SampleCount() const noexcept { return m_sampleCount.load(std::memory_order_relaxed); }

        [[nodiscard]] bool Running() const noexcept { return m_running.load(std::memory_order_acquire); }

    private:
        void _run(std::stop_token stopToken);

        std::jthread m_thread;
        SampleCallback m_callback;

        mutable std::mutex m_mutex;
        std::condition_variable_any m_wake;
        std::atomic<uint32_t> m_intervalMs;
        std::atomic<uint64_t> m_sampleCount{0};
        std::atomic<bool> m_running{false};

        /// Set when the interval changed, so the wait restarts with the new value.
        bool m_intervalChanged{false};
        bool m_refreshRequested{false};
    };
}
