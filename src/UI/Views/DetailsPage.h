// The process details page.
//
// Shows one process in full: its identity, its resource use, and its place in the process tree. The
// original puts this behind the third navigation item, and it is the only place a process can be
// seen on its own rather than as one row among hundreds.
//
// The page follows the process list's selection rather than holding its own, so the two cannot
// disagree about which process is being shown.
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
     * @brief Builds and refreshes the process details page.
     */
    class DetailsPage
    {
    public:
        /**
         * @param coordinator Supplies snapshots; owned by the application.
         */
        explicit DetailsPage(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls the latest snapshot and updates the page. Cheap when nothing changed.
        void Refresh();

        /// Shows a different process. Zero shows the page's empty state.
        void SetPid(uint32_t pid);

        /// The process currently shown.
        [[nodiscard]] uint32_t Pid() const noexcept { return m_pid; }

        /// Applies the configured colour to the chart.
        void SetAccentColor(winrt::Windows::UI::Color color);

        /// Applies the configured stroke width.
        void SetLineWidth(double width);

    private:
        struct DetailRow
        {
            winrt::Microsoft::UI::Xaml::Controls::TextBlock value{nullptr};
        };

        void _buildLayout();

        /**
         * @brief Adds one labelled row to a details column.
         */
        DetailRow _addDetail(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& column,
                             wchar_t const* label);

        /// Writes a value into a retained row.
        void _setDetail(std::vector<DetailRow> const& rows, size_t index, std::string const& text);

        /// Clears every row to a dash, for the empty state.
        void _clearDetails();

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};

        /// The heading: the process's name and its id.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_heading{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_subtitle{nullptr};

        /// Shown when nothing is selected, in place of the details.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_emptyMessage{nullptr};

        /// The area holding the charts and the details, hidden while nothing is selected.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_body{nullptr};

        std::unique_ptr<HistoryChart> m_cpuChart;

        std::vector<DetailRow> m_column1;
        std::vector<DetailRow> m_column2;
        std::vector<DetailRow> m_column3;

        uint32_t m_pid{0};
        uint64_t m_renderedVersion{0};

        /// The snapshot version at which the selected process was last seen, so a process that has
        /// exited can be reported as such rather than silently showing stale figures.
        uint64_t m_lastSeenVersion{0};
    };
}
