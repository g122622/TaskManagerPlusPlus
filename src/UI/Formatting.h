// Human-readable formatting for the values the UI displays.
//
// Kept separate from the views so the formatting rules are unit-testable and are
// defined once. Getting byte units wrong is the classic mistake here: Windows Task
// Manager uses binary multiples (KiB) but labels them KB, and matching that
// convention matters more than being technically correct, because the whole point
// is that the numbers agree with the tool being replaced.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tmpp::ui
{
    /**
     * @brief Formats a byte count using binary multiples with decimal labels.
     *
     * Follows the Windows convention of labelling 1024-based units as KB/MB/GB,
     * so the numbers match what Task Manager and Explorer show.
     *
     * @param bytes Byte count.
     * @return e.g. "1.5 MB", "512 KB", "0 B".
     */
    [[nodiscard]] std::string FormatBytes(uint64_t bytes);

    /**
     * @brief Formats a transfer rate.
     *
     * @param bytesPerSecond Rate in bytes per second.
     * @return e.g. "1.5 MB/s", or an en dash placeholder when no rate is available.
     */
    [[nodiscard]] std::string FormatBytesPerSecond(double bytesPerSecond);

    /**
     * @brief Formats a percentage with one decimal place.
     *
     * @param percent Value in [0, 100] and beyond (a process may exceed 100).
     * @return e.g. "12.3%".
     */
    [[nodiscard]] std::string FormatPercent(double percent);

    /**
     * @brief Formats a CPU percentage for a process.
     *
     * Whole numbers above 10%, one decimal below, matching the way Task Manager
     * avoids a jittery column of decimals for busy processes.
     */
    [[nodiscard]] std::string FormatProcessCpuPercent(double percent);

    /**
     * @brief Formats a count with thousands separators.
     *
     * @return e.g. "1,234".
     */
    [[nodiscard]] std::string FormatCount(uint64_t count);

    /**
     * @brief Formats a duration in the compact form used for uptime.
     *
     * @param seconds Duration.
     * @return e.g. "2d 03:14:15" or "03:14:15".
     */
    [[nodiscard]] std::string FormatDuration(uint64_t seconds);

    /**
     * @brief The placeholder shown where a value is genuinely unavailable.
     *
     * An em dash, not a zero: showing 0 for "we could not read this" is a lie that
     * hides a permissions problem or a failed probe.
     */
    [[nodiscard]] std::string UnavailableValue();

    /**
     * @brief Chooses the name to display for a process.
     *
     * The platform reports no image name for a process with no executable image.
     * On Windows that is the System Idle Process (PID 0), which every task manager
     * nevertheless displays a name for. Synthesising that name is a presentation
     * decision and belongs here, not in the probe: the probe reports what the
     * system says, and this decides how to show it.
     *
     * @param pid Process id.
     * @param imageName Name from the platform, possibly empty.
     */
    [[nodiscard]] std::string ProcessDisplayName(uint32_t pid, std::string_view imageName);
}
