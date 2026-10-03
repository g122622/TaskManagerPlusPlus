// A compact chart for the performance sidebar.
//
// Separate from HistoryChart rather than a mode of it: a sparkline has no header, no
// current-value readout and a fixed small size, so sharing the type would mean every
// caller of one paying for the other's structure.
//
// The value it draws is normalised to a caller-supplied maximum, so the fill always
// reads the same way regardless of the metric. A series with no data leaves the plot
// empty rather than drawing a flat line at the baseline, because a flat line is a
// measurement of zero and "not collected" is not.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <vector>

namespace tmpp::ui
{
    /**
     * @brief A small filled line chart with no axes or labels.
     */
    class Sparkline
    {
    public:
        /**
         * @param color Line and fill colour.
         * @param width Fixed width in effective pixels.
         * @param height Fixed height in effective pixels.
         */
        Sparkline(winrt::Windows::UI::Color color, double width, double height);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /**
         * @brief Replaces the plotted values.
         *
         * @param values Samples in chronological order.
         * @param maximum Value mapped to the top of the plot.
         */
        void SetSeries(std::vector<double> const& values, double maximum);

        /// Clears the plot, leaving it visibly empty.
        void Clear();

        /// Changes the colour, used when a section becomes selected.
        void SetColors(winrt::Windows::UI::Color color, bool muted);

    private:
        void _redraw();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_canvas{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polyline m_line{nullptr};
        winrt::Microsoft::UI::Xaml::Shapes::Polygon m_fill{nullptr};

        std::vector<double> m_values;
        winrt::Windows::UI::Color m_color;

        double m_width{64.0};
        double m_height{24.0};
        double m_maximum{100.0};

        /// Set when a redraw was requested before the canvas had a size, so it can be
        /// retried once layout provides one. Without this, a series set before the
        /// first layout pass would never be drawn.
        bool m_redrawPending{false};
    };
}
