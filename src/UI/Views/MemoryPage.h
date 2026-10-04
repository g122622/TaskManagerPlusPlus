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
#include "Core/Settings.h"
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
        MemoryPage(core::SamplingCoordinator& coordinator, core::ChartStyle const& style);

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

        /**
         * @brief Places the legend entries in as many columns as the width allows.
         *
         * A fixed two per row is what put two across a wide page and left the rest of the width empty.
         *
         * @param availableWidth Width of the legend panel, or zero before the first layout pass.
         */
        void _layoutLegend(double availableWidth);

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

        /// The legend entries themselves, so their row and column can be reassigned when the width
        /// changes.
        std::vector<winrt::Microsoft::UI::Xaml::Controls::StackPanel> m_legendEntries;

        /// How many columns the legend currently uses.
        size_t m_legendColumns{0};

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

        /// The rows themselves, hidden and shown by the expander above them.
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_moduleRowsHost{nullptr};

        /// The clickable header that opens and closes the module list.
        winrt::Microsoft::UI::Xaml::Controls::Button m_moduleToggle{nullptr};

        /// The chevron on that header, which points down when closed and up when open.
        winrt::Microsoft::UI::Xaml::Controls::FontIcon m_moduleChevron{nullptr};

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        uint64_t m_renderedVersion{0};
    };
}
