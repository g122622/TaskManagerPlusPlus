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

        /**
         * @brief Adds a second series, drawn dashed.
         *
         * The disk page needs to show reads and writes together: they share one axis and one window,
         * and comparing them is the point. A dashed line rather than a second colour is what keeps
         * them apart: colour is already carrying the metric, and two colours on one chart would read
         * as two metrics rather than two directions of one.
         *
         * Pass an empty series to remove it.
         *
         * @param series Samples and the window they are drawn against.
         */
        void SetSecondarySeries(ChartSeries const& series);

        /// Updates the large current-value readout.
        void SetCurrentValueText(std::wstring_view text);

        /// Changes the line colour.
        void SetLineColor(winrt::Windows::UI::Color color);

        /// Hides the header, leaving only the plot.
        void SetHeaderVisible(bool visible);


        /**
         * @brief Changes the stroke width of the line.
         *
         * Driven by the user's setting (docs/ROADMAP.md, M1-6).
         */
        void SetLineWidth(double width);

        /**
         * @brief Changes the value the chart's full height represents.
         *
         * Needed when the scale is relative to the data: a throughput chart has no natural maximum,
         * because a device's rate depends on the hardware, so its axis is set from the busiest
         * sample in the window.
         *
         * @param maximum The value at the top of the plot. Zero or less is treated as one, so a
         *        chart without data cannot divide by zero.
         */
        void SetMaximum(double maximum);

    private:
        void _redraw();

        /**
         * @brief Redraws the background grid at the current plot size.
         *
         * The grid is a fixed number of divisions rather than a value scale: the original's lines
         * are evenly spaced guides, not axis ticks, and keeping them fixed means the grid does not
         * shift as values change. What the lines do is give the eye something to measure the curve
         * against, which a plain background does not.
         */
        void _drawGrid();

        /**
         * @brief Maps a value's fraction of the maximum to a y coordinate.
         *
         * The single definition of the plot's vertical scale, used by both the curve and the grid. Written
         * once because the two were computed separately and did not agree: the grid divided the canvas into
         * equal bands while the curve was drawn inside a padded box, so a grid line never sat where its
         * value was.
         *
         * Static and pure so the arithmetic is testable without a visual tree -- the arithmetic is the part
         * that can be wrong, and it was.
         *
         * @param ratio Value as a fraction of the maximum, already clamped to 0..1.
         * @param height Height of the plot area.
         * @param lineWidth Stroke width, half of which is reserved at the bottom.
         * @return The y coordinate, measured from the top of the plot area. A ratio of zero maps to the
         *         bottom edge less half a stroke, so the line's centre sits on zero.
         */
        [[nodiscard]] static double YForRatio(double ratio, double height, double lineWidth);

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid m_header{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_title{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_currentValue{nullptr};

        /// The frame around the plot, built by controls::MakeChartFrame so every chart in the
        /// application is outlined identically.
        winrt::Microsoft::UI::Xaml::Controls::Border m_plotFrame{nullptr};

        /// What sits inside the frame: the grid lines layers first, then the plot.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_plotHost{nullptr};

        /// Grid lines, drawn behind the plot so they stay put while the curve moves.
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_gridCanvas{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::Canvas m_canvas{nullptr};

        winrt::Microsoft::UI::Xaml::Shapes::Polyline m_line{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polygon m_fill{nullptr};

        /// The dashed second line. Hidden until SetSecondarySeries supplies samples, so a chart that
        /// does not use it is unaffected.
        winrt::Microsoft::UI::Xaml::Shapes::Polyline m_secondaryLine{nullptr};
        std::vector<double> m_secondaryValues;

        /// Dash pattern for the second line, in units of the stroke width.
        winrt::Microsoft::UI::Xaml::Media::DoubleCollection m_secondaryDashes{nullptr};

        std::vector<double> m_values;
        winrt::Windows::UI::Color m_color;
        double m_maximum{100.0};

        /// Samples in the full time window. Zero means fit the data instead.
        size_t m_timeSpan{0};

        /// Stroke width of the line, adjustable by the user.
        double m_lineWidth{1.0};

        /// Last diagnostic state reported, so a report is emitted per change rather than per call.
        std::string m_lastDiagnostic;
    };
}
