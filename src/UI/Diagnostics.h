// Diagnostic reporting for the UI layer.
//
// The UI cannot be exercised without a desktop session, so when a control renders
// wrongly the only way to find out why is to have it report its own state. This is the
// channel for that: the UI layer offers messages, and the application decides where
// they go.
//
// The indirection exists to keep the layering honest. UI sits below App and must not
// reach up into it for a log file, so App installs a sink at startup instead. With no
// sink installed the calls do nothing.
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <string_view>

namespace tmpp::ui::diagnostics
{
    /**
     * @brief Installs the destination for UI diagnostic messages.
     *
     * @param sink Called for each message. Pass an empty function to disable.
     */
    void SetSink(std::function<void(std::string_view)> sink);

    /**
     * @brief Reports one line.
     *
     * Cheap enough to leave in a debug build: with no sink installed it returns after
     * one null check.
     */
    void Report(std::string_view message);

    /// True when a sink is installed, so a caller can skip building a costly message.
    [[nodiscard]] bool Enabled() noexcept;

    /**
     * @brief Records that the UI loop has run, and reports it when the loop ran late.
     *
     * This is the measurement that matters for a stutter, and the reason the per-stage timers above can
     * all look healthy while the window still hitches. The loop is scheduled every 100 ms; if it arrives
     * half a second late then the thread was busy or blocked somewhere, and the place it was busy is not
     * necessarily inside any stage that was timed -- a layout pass, a lock held by the sampler, and a
     * compositor stall all happen after the stage that provoked them has already returned.
     *
     * Measuring the gap between invocations catches all of those without having to name any of them.
     *
     * @param scheduledMs The interval the loop is scheduled on.
     */
    void NoteUiTick(double scheduledMs = 100.0);

    /**
     * @brief Times a scope and reports it when it turned out to be slow.
     *
     * A window that stutters is the hardest kind of fault to localise from the outside: every stage
     * returns the right answer and nothing is logged, so the only visible symptom is that the frame did
     * not arrive. Timing the stages and reporting only the ones that overran keeps the record short
     * enough to read while still naming the culprit.
     *
     * The threshold is deliberately well above zero rather than just above a frame: a stage that takes a
     * tenth of a millisecond is not interesting, and reporting every one of them would bury the one that
     * is.
     *
     * Costs two clock reads, so it is cheap enough to leave in a release build -- and with no sink
     * installed it reports nothing at all.
     */
    class ScopedTimer
    {
    public:
        /**
         * @param what Names the stage in the report.
         * @param thresholdMs Shortest duration worth reporting.
         */
        explicit ScopedTimer(std::string_view what, double thresholdMs = 4.0) noexcept;

        ~ScopedTimer();

        ScopedTimer(ScopedTimer const&) = delete;
        ScopedTimer& operator=(ScopedTimer const&) = delete;

    private:
        std::string_view m_what;
        std::chrono::steady_clock::time_point m_start;
        double m_thresholdMs{4.0};
    };
}