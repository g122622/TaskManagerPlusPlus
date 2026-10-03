// The Performance page.
//
// Layout matches Windows 11 Task Manager: a narrow list of hardware sections on the
// left, and the selected section's charts plus hardware details on the right.
//
// The page is laid out with star sizing throughout. Earlier versions used fixed
// heights and wrapped the detail area in a ScrollViewer, which produced a clipped
// chart, a scrollbar, and a details card pushed out of view; the charts now take all
// remaining height and the details card is sized to its content at the bottom.
//
// The section list is a stack of buttons rather than a ListView. Container population
// through ContainerContentChanging depends on the framework's container lifecycle, and
// on this build the rows were never realised -- the list drew empty. Buttons own their
// content directly, so there is no lifecycle to depend on.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "UI/HistoryChart.h"
#include "UI/Sparkline.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the performance page.
     */
    class PerformanceView
    {
    public:
        explicit PerformanceView(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls a new sample and updates the charts. Cheap when nothing changed.
        void Refresh();

    private:
        /**
         * @brief Sections the left-hand list offers.
         *
         * Only the first two have probes today. The rest are listed because the
         * structure is what the user expects, and selecting one states plainly that
         * its metrics are not collected rather than showing an empty chart, which would
         * read as a measurement of zero.
         */
        enum class Section
        {
            Cpu = 0,
            Memory,
            Disk,
            Network,
            Gpu,
            Count,
        };

        /// Description of one section, so the sidebar and the detail area agree.
        struct SectionSpec
        {
            wchar_t const* title;
            wchar_t const* glyph; ///< Segoe Fluent Icons glyph.
            bool hasData;
            winrt::Windows::UI::Color color;
        };

        void _buildLayout();

        /// Rebuilds the detail area for a section.
        void _selectSection(Section section);

        /// Applies the selected/unselected styling to every sidebar row.
        void _updateSelectionVisuals();

        /// Writes the current values into the existing detail rows.
        void _updateDetails(domain::SystemView const& system);

        /// Feeds the sidebar sparklines from the current history.
        void _updateSidebarCharts(domain::HistoryView const& history);

        [[nodiscard]] static std::vector<SectionSpec> const& _sections();

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_sidebar{nullptr};

        /// Detail area, rebuilt per section. Holds the charts and the details card.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_detailHost{nullptr};

        /// Sidebar rows, indexed by Section.
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> m_buttons;
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Grid> m_rows;
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_rowTitles;
        std::vector<std::unique_ptr<Sparkline>> m_rowSparklines;

        std::unique_ptr<HistoryChart> m_primaryChart;
        std::unique_ptr<HistoryChart> m_secondaryChart;

        /// The details card, created once per section and whose value rows are updated
        /// in place. Rebuilding it per refresh would append controls several times a
        /// second and grow without bound.
        winrt::Microsoft::UI::Xaml::Controls::Border m_detailsCard{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_detailValues;

        Section m_selected{Section::Cpu};
        uint64_t m_renderedVersion{0};
    };
}
