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
}