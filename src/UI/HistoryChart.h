// A real-time line chart drawn as geometry.
//
// Win2D would be the choice for the finished charts (see docs/ROADMAP.md, M1-5),
// but it adds a NuGet dependency and a drawing surface. For the first working
// version a Polyline in a Canvas is enough: the data sets are small (a few hundred
// points), the shape is simple, and it needs no extra package or render loop.
//
// Exactly one point is emitted per retained sample, and the point count is bounded
// by the history capacity, so the per-frame cost is constant no matter how long the
// application runs.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <string>
#include <vector>


namespace tmpp::ui
{
    /**
     * @brief A scrolling line chart with a fixed Y range.
     */
    class HistoryChart
    {
    public:
        /**
         * @param title Caption shown above the chart.
         * @param lineColor Series colour. This is the hook the colour
         *        customisation feature will drive.
         * @param maximum Fixed Y-axis maximum. A fixed range keeps the shape of
         *        the curve comparable between moments; an auto-fitting axis makes
         *        a 2% change look like a spike.
         */
        HistoryChart(std::wstring_view title, winrt::Windows::UI::Color lineColor, double maximum);

        /// The root element to place in a panel.
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

    private:
        void _redraw();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_canvas{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polyline m_line{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polygon m_fill{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_currentValue{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_title{nullptr};

        std::vector<double> m_values;
        winrt::Windows::UI::Color m_color;
        double m_maximum{100.0};
        bool m_hasData{false};
    };
}
