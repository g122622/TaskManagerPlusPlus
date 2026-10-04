// The disk page.
//
// Built to the same shape as the CPU and memory pages: a heading naming the device, the usage
// chart, then the figures in three columns. Reusing that layout rather than inventing a second one
// is what makes the performance page feel like one page rather than three.
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
     * @brief Builds and refreshes the disk performance page.
     */
    class DiskPage
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
        DiskPage(core::SamplingCoordinator& coordinator, core::ChartStyle const& style);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls the latest snapshot and updates the page. Cheap when nothing changed.
        void Refresh();

        /// Applies the configured colour to the charts.
        void SetAccentColor(winrt::Windows::UI::Color color);

        /// Applies the configured stroke width.
        void SetLineWidth(double width);

        /// Selects which device the page reports. Out-of-range falls back to the first.
        void SetDeviceIndex(size_t index);

        /// Names the device in the heading, as the sidebar row does.
        void SetDeviceLabel(std::wstring_view label);

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
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_modelCaption{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_caption{nullptr};

        /// Percentage of the time the device spent servicing requests. The original puts this first,
        /// and it is the figure that explains a slow response with low throughput.
        std::unique_ptr<HistoryChart> m_activeChart;

        /// Transfer rate. A second chart rather than a second line on the first, because the two
        /// quantities have different units and different natural maxima: active time is a share of
        /// one device, while a transfer rate depends on the hardware. Plotting them together would
        /// need an axis that means nothing for one of them.
        std::unique_ptr<HistoryChart> m_transferChart;

        /// The caption above each chart, retained so the peak figure can be updated.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_activeCaption{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_transferCaption{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_transferPeakLabel{nullptr};

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        size_t m_deviceIndex{0};

        /// The last device list that was successfully read.
        ///
        /// A sample can come back with no devices when the probe's handle query fails intermittently.
        /// Showing dashes for that one frame makes the whole page flicker empty, so the previous
        /// reading stands until a new one arrives.
        std::vector<domain::DiskActivity> m_lastDisks;
        std::wstring m_deviceLabel;
        uint64_t m_renderedVersion{0};
    };
}
