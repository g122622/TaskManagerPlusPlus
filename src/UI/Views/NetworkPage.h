// The network page.
//
// Same shape as the other performance pages: a heading naming the adapter, the throughput chart,
// then the figures. The chart here has a real maximum, because a link has a known speed -- unlike
// the disk page, whose axis has to be scaled to the data.
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
     * @brief Builds and refreshes the network performance page.
     */
    class NetworkPage
    {
    public:
        /**
         * @param coordinator Supplies snapshots; owned by the application.
         */
        explicit NetworkPage(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls the latest snapshot and updates the page. Cheap when nothing changed.
        void Refresh();

        /// Applies the configured colour to the chart.
        void SetAccentColor(winrt::Windows::UI::Color color);

        /// Applies the configured stroke width.
        void SetLineWidth(double width);

        /// Selects which adapter the page reports. Out-of-range falls back to the first.
        void SetAdapterIndex(size_t index);

        /// Names the adapter in the heading, as the sidebar row does.
        void SetAdapterLabel(std::wstring_view label);

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
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_linkCaption{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_caption{nullptr};

        std::unique_ptr<HistoryChart> m_chart;

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        size_t m_adapterIndex{0};
        std::wstring m_adapterLabel;
        uint64_t m_renderedVersion{0};
    };
}
