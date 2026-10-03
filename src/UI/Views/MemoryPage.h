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

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_heading{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_caption{nullptr};

        std::unique_ptr<HistoryChart> m_chart;

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        uint64_t m_renderedVersion{0};
    };
}
