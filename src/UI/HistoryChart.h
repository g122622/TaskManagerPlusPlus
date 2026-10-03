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
#include <vector>

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
         * @brief Replaces the plotted values.
         *
         * @param values Samples in chronological order.
         */
        void SetSeries(std::vector<double> const& values);

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
    };
}
