// The value every chart is given.
//
// Bundling the samples with the time window they belong to is the point of this type. The
// window was previously a separate setter on one chart, which meant three of the four charts
// were never told about it and drew their few samples stretched across the entire width --
// reading as a settled history that did not exist yet, then visibly compressing as real
// samples arrived.
//
// Passing the two together makes that omission impossible: a caller cannot supply samples
// without also stating what part of the axis they occupy.
#pragma once

#include <cstddef>
#include <vector>

namespace tmpp::ui
{
    /**
     * @brief A series of samples and the time window it is drawn against.
     */
    struct ChartSeries
    {
        /// Samples in chronological order, oldest first.
        std::vector<double> values;

        /**
         * @brief Number of samples that represents the full width of the chart.
         *
         * Charts are right-anchored: the newest sample sits at the right edge and older
         * samples extend leftwards, so a partly filled window occupies only its right-hand
         * part. Once values.size() reaches this, the line spans the whole width and scrolls.
         *
         * Zero means "fill the width with whatever is here", which is only appropriate for a
         * chart with no time axis at all. Every chart that represents a period must set it.
         */
        size_t windowSamples{0};

        [[nodiscard]] bool Empty() const noexcept { return values.empty(); }
        [[nodiscard]] size_t Size() const noexcept { return values.size(); }

        /// True when the window is known, so the chart can anchor rather than fit.
        [[nodiscard]] bool HasWindow() const noexcept { return windowSamples > 1; }
    };
}
