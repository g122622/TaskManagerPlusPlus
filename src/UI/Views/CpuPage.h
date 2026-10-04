// The CPU page of the performance view.
//
// Mirrors the layout of Windows 11 Task Manager's CPU tab: the processor name and live
// clock across the top, a grid of one chart per logical processor below it, and a
// three-column panel of hardware details at the bottom.
//
// The three-column panel is the part worth explaining. The original splits its details
// into three fixed groups -- utilisation and uptime, then process/thread/handle counts
// with speed, then the static topology. They are laid out as three star columns so the
// groups stay aligned as the window resizes, which is what a table-like block of
// label/value pairs needs and what a wrapping panel would not give.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "Core/SamplingCoordinator.h"
#include "UI/Charts/CoreGrid.h"
#include "UI/Charts/HistoryChart.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the CPU detail page.
     */
    class CpuPage
    {
    public:
        explicit CpuPage(core::SamplingCoordinator& coordinator);

        /// The root element to place in a star-sized cell.
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pushes a new sample. Cheap when nothing changed.
        void Refresh();

        /// Applies the series colour, driven by the colour-customisation feature.
        void SetAccentColor(winrt::Windows::UI::Color color);

        /// Applies the configured stroke width to every chart on the page.
        void SetLineWidth(double width);

    private:
        void _buildLayout();

        /// One detail row: a label on the left and a value on the right, returning the
        /// value block so it can be updated in place.
        struct DetailRow
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock label{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock value{nullptr};
        };

        /// Adds a detail row to a column and returns its value block.
        DetailRow _addDetail(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& column,
                             wchar_t const* label);

        /// Writes the current sample into the retained detail rows.
        void _updateDetails(domain::SystemView const& system);

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};

        /// The section heading, which reads "CPU".
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_processorName{nullptr};

        /// The processor's marketing name, right-aligned beside the heading.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_processorModel{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_chartCaption{nullptr};

        std::unique_ptr<CoreGrid> m_coreGrid;
        std::unique_ptr<HistoryChart> m_totalChart;

        // Detail rows, in the order the original groups them.
        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        uint64_t m_renderedVersion{0};
    };
}