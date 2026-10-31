#pragma once

#include <algorithm>

namespace tmpp::ui
{
    // Full scale touches the top edge; zero leaves room for the bottom half of the stroke.
    [[nodiscard]] inline double ChartYForRatio(double ratio, double height, double lineWidth) noexcept
    {
        double const drawable = (std::max)(0.0, height - (std::max)(0.0, lineWidth) / 2.0);
        return drawable * (1.0 - std::clamp(ratio, 0.0, 1.0));
    }
}
