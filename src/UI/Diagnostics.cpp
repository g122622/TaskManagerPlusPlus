#include "UI/Diagnostics.h"

#include <algorithm>
#include <cstdio>

namespace tmpp::ui::diagnostics
{
    namespace
    {
        /// The installed destination. Empty when diagnostics are off.
        std::function<void(std::string_view)> g_sink;

        /**
         * @brief The stages that have overrun the threshold, and how often each has.
         *
         * A stage that overruns on every refresh would otherwise write its line ten times a second and
         * bury everything else -- including the one-off hitch that is being looked for. Each slow stage
         * is therefore reported on its first and its hundredth occurrence only, which shows both that it
         * started and that it is still going on.
         */
        constexpr uint64_t REPORT_INTERVAL = 100;
        constexpr size_t MAX_TRACKED_STEPS = 16;

        struct StepRecord
        {
            std::string_view what;
            double worstMs{0.0};
            double totalMs{0.0};
            uint64_t count{0};
        };

        StepRecord g_steps[MAX_TRACKED_STEPS];
        size_t g_stepCount = 0;
    }

    void SetSink(std::function<void(std::string_view)> sink)
    {
        g_sink = std::move(sink);
    }

    void Report(std::string_view message)
    {
        if (g_sink)
        {
            g_sink(message);
        }
    }

    bool Enabled() noexcept
    {
        return static_cast<bool>(g_sink);
    }

    void NoteUiTick(double scheduledMs)
    {
        if (!g_sink)
        {
            return;
        }

        // Declared here rather than in the anonymous namespace so that a test or a second UI loop could
        // not share the state by accident; the loop this measures is the application's own.
        static std::chrono::steady_clock::time_point last{};
        static uint64_t tick = 0;
        static double worstLateness = 0.0;

        auto const now = std::chrono::steady_clock::now();
        auto const previous = last;
        last = now;

        if (tick == 0)
        {
            ++tick;
            return;
        }

        ++tick;

        double const gapMs = std::chrono::duration<double, std::milli>(now - previous).count();

        // A quarter of a second is where a hitch becomes visible as one: the loop is meant to run ten
        // times a second, so three missed slots in a row means the thread was not merely busy but
        // blocked.
        constexpr double LATE_MS = 250.0;
        if (gapMs < LATE_MS)
        {
            return;
        }

        worstLateness = (std::max)(worstLateness, gapMs);

        // Reported every time rather than throttled: a late tick is rare by construction, and each one
        // is a separate event with its own cause.
        char buffer[192]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "ui loop late: %.0f ms since the previous tick (scheduled every %.0f, worst %.0f)",
                      gapMs,
                      scheduledMs,
                      worstLateness);
        g_sink(buffer);
    }

    ScopedTimer::ScopedTimer(std::string_view what, double thresholdMs) noexcept
        : m_what(what), m_start(std::chrono::steady_clock::now()), m_thresholdMs(thresholdMs)
    {
    }

    ScopedTimer::~ScopedTimer()
    {
        if (!g_sink)
        {
            return;
        }

        double const millis =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start).count();

        if (millis < m_thresholdMs)
        {
            return;
        }

        // Keyed on the name's text rather than its address. Callers pass string literals, so the address
        // is stable in practice, but comparing the text is what makes the lookup independent of that
        // assumption.
        StepRecord* record = nullptr;
        for (size_t i = 0; i < g_stepCount; ++i)
        {
            if (g_steps[i].what == m_what)
            {
                record = &g_steps[i];
                break;
            }
        }

        if (record == nullptr)
        {
            // A full table is left alone rather than evicting: the entries in it are the stages that were
            // slow first, and dropping them would make a recurring problem look like a new one every
            // time the table filled up.
            if (g_stepCount >= MAX_TRACKED_STEPS)
            {
                return;
            }

            record = &g_steps[g_stepCount++];
            record->what = m_what;
        }

        ++record->count;
        record->totalMs += millis;
        record->worstMs = (std::max)(record->worstMs, millis);

        if (record->count != 1 && record->count % REPORT_INTERVAL != 0)
        {
            return;
        }

        char buffer[256]{};
        std::snprintf(buffer,
                      sizeof(buffer),
                      "slow: %.*s -- %.1f ms this time, worst %.1f, mean %.1f, %llu times",
                      static_cast<int>(m_what.size()),
                      m_what.data(),
                      millis,
                      record->worstMs,
                      record->totalMs / static_cast<double>(record->count),
                      static_cast<unsigned long long>(record->count));
        g_sink(buffer);
    }
}
