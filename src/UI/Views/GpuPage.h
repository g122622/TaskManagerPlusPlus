// The GPU page.
//
// Same shape as the other performance pages. The chart plots utilisation, which is a real
// percentage, while the memory figures sit in the details: an adapter's memory is a quantity, not a
// proportion of a fixed maximum, and the two would need separate axes to share one chart.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/SamplingCoordinator.h"
#include "UI/Charts/HistoryChart.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and refreshes the GPU performance page.
     */
    class GpuPage
    {
    public:
        /**
         * @param coordinator Supplies snapshots; owned by the application.
         */
        explicit GpuPage(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls the latest snapshot and updates the page. Cheap when nothing changed.
        void Refresh();

        /// Applies the configured colour to the chart.
        void SetAccentColor(winrt::Windows::UI::Color color);

        /// Applies the configured stroke width.
        void SetLineWidth(double width);

    private:
        struct DetailRow
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock label{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock value{nullptr};
        };

        void _buildLayout();

        DetailRow _addDetail(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& column,
                             wchar_t const* label);

        void _updateDetails(domain::SystemView const& system);

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_heading{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_memoryCaption{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_caption{nullptr};

        /// Utilisation, as a percentage. A real proportion, so its axis is fixed.
        std::unique_ptr<HistoryChart> m_chart;

        /// Dedicated memory in use, in bytes. A separate chart because it answers a different
        /// question from utilisation, and the two have different units: how hard the adapter is
        /// working, and how much of its own memory is committed. One axis cannot carry both.
        std::unique_ptr<HistoryChart> m_memoryChart;

        /// The caption above the memory chart, and the figure at the top of its axis.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_memoryChartCaption{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_memoryChartPeak{nullptr};

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        uint64_t m_renderedVersion{0};
    };
}
