// The Memory page of the performance view.
//
// Mirrors the layout of Windows 11 Task Manager's Memory tab: a heading, one large usage
// chart, and a panel of memory figures below it.
//
// Only the figures this application can actually read are shown with values. The original
// also reports cached, pool and hardware figures, which need probes that do not exist yet
// (docs/ROADMAP.md, M2); those rows are present so the panel has the same shape, and they
// state plainly that they are not collected rather than showing a zero. A zero there would
// be indistinguishable from a real reading of nothing in use.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/SamplingCoordinator.h"
#include "UI/Charts/HistoryChart.h"
#include "UI/Charts/MemoryCompositionBar.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the memory detail page.
     */
    class MemoryPage
    {
    public:
        explicit MemoryPage(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pushes a new sample. Cheap when nothing changed.
        void Refresh();

        /// Applies the series colour, driven by the colour-customisation feature.
        void SetAccentColor(winrt::Windows::UI::Color color);

        /// Applies the configured stroke width to the chart.
        void SetLineWidth(double width);

    private:
        /// One detail row, retaining the value block so it can be updated in place.
        struct DetailRow
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock label{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock value{nullptr};
        };

        void _buildLayout();

        DetailRow _addDetail(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& column,
                             wchar_t const* label);

        void _updateDetails(domain::SystemView const& system);

        /// Feeds the composition strip and its legend from the page-list breakdown.
        void _updateComposition(domain::SystemView const& system);

        /**
         * @brief Adds the memory-module list, built from the firmware's SMBIOS table.
         *
         * Built once rather than per sample: the modules a machine has do not change while it runs.
         */
        void _addModuleList();

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_heading{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_caption{nullptr};

        /// Installed capacity and type, shown at the right of the heading.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_installedCaption{nullptr};

        std::unique_ptr<HistoryChart> m_chart;

        /// The proportional strip under the chart, showing how memory is distributed.
        std::unique_ptr<MemoryCompositionBar> m_composition;

        /// Label row under the strip, naming each segment's share.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_compositionLegend{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_legendValues;

        /// One row per memory module, built once from the firmware table.
        struct ModuleRow
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock title{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock detail{nullptr};
        };

        std::vector<ModuleRow> m_moduleRows;

        /// The heading above the module list, which says how many slots are in use.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_slotsCaption{nullptr};

        /// The list of memory modules, filled once because the firmware does not change.
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_moduleList{nullptr};

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        uint64_t m_renderedVersion{0};
    };
}
