// A real-time line chart that fills the space it is given.
//
// The chart has no intrinsic height. Early versions fixed the plot at 140 pixels,
// which left the curve clipped and a band of empty space above it whenever the window
// was taller than that; the plot area is now a star row, so the chart grows and
// shrinks with the window and redraws on every size change.
//
// Points are drawn in the canvas's own coordinates, computed from its actual size at
// redraw time, so nothing needs measuring ahead of layout.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "UI/Charts/ChartSeries.h"

namespace tmpp::ui
{
    /**
     * @brief A scrolling line chart with a fixed value range.
     */
    class HistoryChart
    {
    public:
        /**
         * @param title Caption shown above the plot.
         * @param lineColor Series colour. This is the hook the colour-customisation
         *        feature will drive (docs/ROADMAP.md, M1-6).
         * @param maximum Fixed maximum. A fixed range keeps the shape of the curve
         *        comparable between moments; an auto-fitting axis makes a small
         *        change look like a spike.
         */
        HistoryChart(std::wstring_view title, winrt::Windows::UI::Color lineColor, double maximum);

        /// The root element to place in a star-sized cell.
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /**
         * @brief Replaces the plotted series.
         *
         * The series carries its own time window, so the chart cannot be given samples
         * without also being told what part of the axis they occupy. The x axis is a fixed
         * window rather than a fit to the data: with a window of sixty and three samples,
         * the line occupies the right-hand twentieth of the width and the rest stays empty.
         *
         * @param series Samples and the window they are drawn against.
         */
        void SetSeries(ChartSeries const& series);

        /// Updates the large current-value readout.
        void SetCurrentValueText(std::wstring_view text);

        /// Changes the line colour.
        void SetLineColor(winrt::Windows::UI::Color color);

        /// Hides the header, leaving only the plot.
        void SetHeaderVisible(bool visible);

    private:
        void _redraw();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid m_header{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_title{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_currentValue{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_canvas{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polyline m_line{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polygon m_fill{nullptr};

        std::vector<double> m_values;
        winrt::Windows::UI::Color m_color;
        double m_maximum{100.0};

        /// Samples in the full time window. Zero means fit the data instead.
        size_t m_timeSpan{0};

        /// Diagnostic bookkeeping: the last reported (points, size) state, so a report is
        /// emitted on each change rather than on every call.
        std::tuple<size_t, int, int> m_lastReportedState{0, 0, 0};
        bool m_hasReportedState{false};

    public:
        /**
         * @brief Number of points currently held.
         *
         * Exposed for diagnostics: whether a blank chart means missing data or a
         * zero-sized canvas cannot be told from the outside, and the two have entirely
         * different fixes.
         */
        [[nodiscard]] size_t PointCount() const noexcept { return m_values.size(); }

        /// The canvas size the last redraw saw, as width * 100000 + height.
        [[nodiscard]] double CanvasWidth() const noexcept { return m_canvas.ActualWidth(); }
        [[nodiscard]] double CanvasHeight() const noexcept { return m_canvas.ActualHeight(); }

        /// Height of the root grid, of the header, and whether the header is collapsed.
        ///
        /// Exposed for diagnostics: a cell canvas too short to draw in has two possible
        /// causes -- the cell itself is too small, or the header is still occupying part
        /// of it -- and the two have entirely different fixes.
        [[nodiscard]] double RootHeight() const noexcept { return m_root.ActualHeight(); }
        [[nodiscard]] double HeaderHeight() const noexcept { return m_header.ActualHeight(); }
        [[nodiscard]] bool HeaderVisible() const noexcept
        {
            return m_header.Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Collapsed;
        }
    };
}
