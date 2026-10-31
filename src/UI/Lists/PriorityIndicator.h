#pragma once

#include <cstdint>

namespace tmpp::ui
{
    struct PriorityColor { uint8_t red; uint8_t green; uint8_t blue; };

    [[nodiscard]] inline PriorityColor PriorityIndicatorColor(int32_t basePriority) noexcept
    {
        if (basePriority >= 24) return {229, 72, 77};   // Realtime: red.
        if (basePriority >= 13) return {242, 140, 40};  // High: orange.
        if (basePriority >= 10) return {245, 215, 66};  // Above normal: bright yellow.
        if (basePriority >= 8) return {41, 153, 165};   // Normal: teal.
        if (basePriority >= 6) return {75, 128, 205};   // Below normal: blue.
        return {139, 139, 139};                       // Idle/low: grey.
    }
}
