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
        explicit DiskPage(core::SamplingCoordinator& coordinator);

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

        /// Read throughput on the primary axis. Write throughput is drawn on the same chart as a
        /// second series when the component supports it; until then the figures are in the details.
        std::unique_ptr<HistoryChart> m_chart;

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        size_t m_deviceIndex{0};
        std::wstring m_deviceLabel;
        uint64_t m_renderedVersion{0};
    };
}
