#include "Core/BackgroundSampler.h"

#include <utility>

namespace tmpp::core
{
    BackgroundSampler::BackgroundSampler(uint32_t intervalMs, SampleCallback callback)
        : m_callback(std::move(callback)),
          m_intervalMs(domain::sampling::ClampInterval(intervalMs))
    {
    }

    BackgroundSampler::~BackgroundSampler()
    {
        Stop();
    }

    void BackgroundSampler::Start()
    {
        if (m_running.load(std::memory_order_acquire))
        {
            return;
        }

        m_running.store(true, std::memory_order_release);
        // std::jthread supplies the stop_token and joins on destruction, but Stop()
        // is still explicit so the owner can control ordering at shutdown.
        m_thread = std::jthread([this](std::stop_token stopToken) { _run(stopToken); });
    }

    void BackgroundSampler::Stop()
    {
        if (!m_thread.joinable())
        {
            m_running.store(false, std::memory_order_release);
            return;
        }

        m_thread.request_stop();
        // Wake the loop so it observes the stop request instead of waiting out the
        // current interval.
        m_wake.notify_all();

        m_thread.join();
        m_running.store(false, std::memory_order_release);
    }

    void BackgroundSampler::RequestRefresh()
    {
        {
            std::lock_guard const lock(m_mutex);
            m_refreshRequested = true;
        }
        m_wake.notify_all();
    }

    void BackgroundSampler::SetInterval(uint32_t intervalMs)
    {
        uint32_t const clamped = domain::sampling::ClampInterval(intervalMs);
        if (m_intervalMs.load(std::memory_order_relaxed) == clamped)
        {
            return;
        }

        {
            std::lock_guard const lock(m_mutex);
            m_intervalMs.store(clamped, std::memory_order_relaxed);
            m_intervalChanged = true;
        }
        // Wake immediately: the whole point of this method is that the new cadence
        // applies now, not after the old interval expires.
        m_wake.notify_all();
    }

    void BackgroundSampler::_run(std::stop_token stopToken)
    {
        // The first sample is taken straight away so the UI has data to draw on its
        // first frame instead of showing an empty chart for one interval.
        if (m_callback)
        {
            m_callback(WakeReason::Initial);
            m_sampleCount.fetch_add(1, std::memory_order_relaxed);
        }

        while (!stopToken.stop_requested())
        {
            auto const interval = std::chrono::milliseconds(m_intervalMs.load(std::memory_order_relaxed));

            WakeReason reason = WakeReason::Interval;
            bool sampleNow = false;

            {
                std::unique_lock lock(m_mutex);

                // condition_variable_any is used rather than condition_variable so
                // that the stop_token itself can interrupt the wait, alongside the
                // explicit notifications.
                bool const woken = m_wake.wait_for(lock, stopToken, interval, [this, stopToken] {
                    return stopToken.stop_requested() || m_intervalChanged || m_refreshRequested;
                });

                if (stopToken.stop_requested())
                {
                    break;
                }

                if (m_refreshRequested)
                {
                    m_refreshRequested = false;
                    reason = WakeReason::Refresh;
                    sampleNow = true;
                }
                else if (m_intervalChanged)
                {
                    m_intervalChanged = false;
                    reason = WakeReason::IntervalChanged;
                    sampleNow = true;
                }
                else if (!woken)
                {
                    // The wait timed out, so this is a normal scheduled tick.
                    reason = WakeReason::Interval;
                    sampleNow = true;
                }
            }

            if (sampleNow && m_callback)
            {
                m_callback(reason);
                m_sampleCount.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
}
