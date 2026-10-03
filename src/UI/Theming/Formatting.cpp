#include "UI/Theming/Formatting.h"

#include <array>
#include <cmath>
#include <cstdio>

namespace tmpp::ui
{
    namespace
    {
        /// Binary prefixes, labelled with the decimal names Windows uses.
        constexpr std::array<char const*, 6> BYTE_UNITS{"B", "KB", "MB", "GB", "TB", "PB"};

        constexpr uint64_t BYTES_PER_UNIT = 1024;

        /**
         * @brief Formats with a fixed number of decimals.
         */
        [[nodiscard]] std::string _formatFixed(double value, int decimals)
        {
            char buffer[64]{};
            std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
            return buffer;
        }

        /**
         * @brief Splits a value into a scaled magnitude and a unit index.
         */
        struct ScaledValue
        {
            double magnitude{0.0};
            size_t unitIndex{0};
        };

        [[nodiscard]] ScaledValue _scale(uint64_t bytes) noexcept
        {
            ScaledValue scaled;
            scaled.magnitude = static_cast<double>(bytes);

            while (scaled.magnitude >= static_cast<double>(BYTES_PER_UNIT) &&
                   scaled.unitIndex + 1 < BYTE_UNITS.size())
            {
                scaled.magnitude /= static_cast<double>(BYTES_PER_UNIT);
                ++scaled.unitIndex;
            }
            return scaled;
        }
    }

    std::string FormatBytes(uint64_t bytes)
    {
        ScaledValue scaled = _scale(bytes);

        // Bytes are always whole; larger units show one decimal, which is the
        // precision Task Manager displays.
        int decimals = (scaled.unitIndex == 0) ? 0 : 1;

        // Scaling by dividing can leave a value that rounds up to 1024.0 at its
        // unit boundary (1048575 bytes becomes "1024.0 KB"). That reads as a
        // contradiction, so promote to the next unit instead.
        constexpr double ROUNDING_THRESHOLD = 1023.95;
        if (scaled.unitIndex + 1 < BYTE_UNITS.size() && scaled.magnitude >= ROUNDING_THRESHOLD)
        {
            scaled.magnitude /= static_cast<double>(BYTES_PER_UNIT);
            ++scaled.unitIndex;
            decimals = 1;
        }

        std::string result = _formatFixed(scaled.magnitude, decimals);
        result += ' ';
        result += BYTE_UNITS[scaled.unitIndex];
        return result;
    }

    std::string FormatBytesPerSecond(double bytesPerSecond)
    {
        if (bytesPerSecond < 0.0 || !std::isfinite(bytesPerSecond))
        {
            return UnavailableValue();
        }

        return FormatBytes(static_cast<uint64_t>(bytesPerSecond)) + "/s";
    }

    std::string FormatPercent(double percent)
    {
        if (!std::isfinite(percent))
        {
            return UnavailableValue();
        }
        return _formatFixed(percent, 1) + "%";
    }

    std::string FormatProcessCpuPercent(double percent)
    {
        if (!std::isfinite(percent))
        {
            return UnavailableValue();
        }

        // Whole numbers once the figure is large enough that a decimal is noise.
        int const decimals = (percent < 10.0) ? 1 : 0;
        return _formatFixed(percent, decimals) + "%";
    }

    std::string FormatCount(uint64_t count)
    {
        std::string const digits = std::to_string(count);

        std::string result;
        result.reserve(digits.size() + (digits.size() / 3));

        size_t const firstGroup = digits.size() % 3;
        for (size_t i = 0; i < digits.size(); ++i)
        {
            // Insert a separator before every group of three, except at the start.
            if (i > 0 && (i - firstGroup) % 3 == 0)
            {
                result.push_back(',');
            }
            result.push_back(digits[i]);
        }

        return result;
    }

    std::string FormatDuration(uint64_t seconds)
    {
        uint64_t const days = seconds / 86400;
        uint64_t const hours = (seconds % 86400) / 3600;
        uint64_t const minutes = (seconds % 3600) / 60;
        uint64_t const remainingSeconds = seconds % 60;

        char buffer[64]{};
        if (days > 0)
        {
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%llud %02llu:%02llu:%02llu",
                          static_cast<unsigned long long>(days),
                          static_cast<unsigned long long>(hours),
                          static_cast<unsigned long long>(minutes),
                          static_cast<unsigned long long>(remainingSeconds));
        }
        else
        {
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%02llu:%02llu:%02llu",
                          static_cast<unsigned long long>(hours),
                          static_cast<unsigned long long>(minutes),
                          static_cast<unsigned long long>(remainingSeconds));
        }

        return buffer;
    }

    std::string UnavailableValue()
    {
        // An em dash, matching how Task Manager blanks out a value it cannot read.
        return "\xE2\x80\x94";
    }

    std::string ProcessDisplayName(uint32_t pid, std::string_view imageName)
    {
        if (!imageName.empty())
        {
            return std::string{imageName};
        }

        // PID 0 is the System Idle Process, which has no executable image. The
        // platform reports an empty name for it; every task manager still shows a
        // label, so one is supplied here.
        constexpr uint32_t SYSTEM_IDLE_PROCESS_PID = 0;
        if (pid == SYSTEM_IDLE_PROCESS_PID)
        {
            return "System Idle Process";
        }

        // Any other unnamed process is unexpected. Showing the PID is more useful
        // than showing nothing, and it makes the anomaly visible.
        return "PID " + std::to_string(pid);
    }
}
