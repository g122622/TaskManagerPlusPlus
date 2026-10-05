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
#include "Core/Settings.h"
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
        /**
         * @brief Builds the page with its chart style already applied.
         *
         * The style is taken here rather than pushed in afterwards because the page is built lazily, on
         * first selection: a page that waits to be told would draw its charts in the defaults until
         * something happened to notify it, and nothing does between its construction and its first frame.
         *
         * @param coordinator Source of samples.
         * @param style Colour and stroke width for the charts.
         */
        NetworkPage(core::SamplingCoordinator& coordinator, core::ChartStyle const& style);

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

        /**
         * @brief The adapter's model, right-aligned on the heading row.
         *
         * It names the hardware the page is describing, which is what the heading beside it is too general
         * to say: a machine can have several adapters and "Wi-Fi" does not distinguish between them.
         *
         * It was the link speed before, which the detail panel already states twice, so the heading row was
         * carrying a figure the reader could find below rather than the one thing that identifies the
         * device.
         */
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_adapterModel{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_caption{nullptr};

        /**
         * @brief The figure at the top of the chart's value axis.
         *
         * The axis is scaled to the busiest of the two directions rather than to a fixed proportion, so
         * this states what that top line is worth. It is not a percentage: the link's capacity is a
         * separate figure, shown in the heading, and the chart plots throughput rather than a share of it.
         */
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_peakLabel{nullptr};

        std::unique_ptr<HistoryChart> m_chart;

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        size_t m_adapterIndex{0};
        std::wstring m_adapterLabel;
        uint64_t m_renderedVersion{0};
    };
}
