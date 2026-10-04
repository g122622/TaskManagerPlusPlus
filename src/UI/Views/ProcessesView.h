// The Processes page.
//
// Rows are hosted by RowHost, which builds visuals only for the rows in view. With
// hundreds of processes on a typical machine, creating a control tree per process
// up front would cost hundreds of milliseconds before the first frame; this keeps
// the work proportional to what is actually on screen.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "Platform/Windows/WindowsProcessActions.h"
#include "UI/Lists/ProcessIconCache.h"
#include "UI/Lists/ProcessListModel.h"
#include "UI/Lists/RowHost.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the process list page.
     *
     * Not a WinRT runtime class: it composes controls rather than deriving from one,
     * which avoids needing an IDL for a class with no XAML counterpart.
     */
    class ProcessesView
    {
    public:
        explicit ProcessesView(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls a new snapshot and repaints. Cheap when nothing changed.
        void Refresh();

        /**
         * @brief Sets the filter the list applies.
         *
         * Supplied by the window, whose title bar hosts the search box: the field is reachable from every
         * page, so it cannot belong to this one.
         */
        void SetFilter(std::string filter);

        /**
         * @brief Adopts remembered column widths.
         *
         * Called once at construction with whatever the settings file held. A width of zero, or an entry
         * outside the column's bounds, falls back to the column's own default so a hand-edited or
         * truncated file cannot leave a column unreadable.
         *
         * @param widths One per column, in the column table's order. Shorter than the table is allowed.
         */
        void SetColumnWidths(std::vector<double> const& widths);

        /**
         * @brief Called when the user drags a column's edge.
         *
         * The view does not write settings itself: the application owns the file and decides when to save.
         * The whole set is reported rather than the one that changed, so the file cannot end up holding a
         * mixture of current and stale widths.
         */
        void SetColumnWidthHandler(std::function<void(std::vector<double>)> handler);

        /**
         * @brief The process the user has selected, or zero when none is.
         *
         * The Details page shows one process at a time, so the selection has to live somewhere both
         * pages can see. It is held here because this is the only page that can set it.
         */
        [[nodiscard]] uint32_t SelectedPid() const noexcept { return m_selectedPid; }

        /**
         * @brief Called when the selection changes, so a detail page can follow it.
         */
        void SetSelectionHandler(std::function<void(uint32_t)> handler);

        /**
         * @brief Called when the user asks to end a process.
         *
         * The view does not terminate anything itself: ending a process is an application-level act
         * that has to be able to report its outcome back to the user. The handler returns the outcome
         * so the view can show it.
         *
         * @param pid Process to end.
         * @param entireTree True to end every descendant as well.
         */
        void SetTerminateHandler(std::function<platform::ProcessActionResult(uint32_t, bool)> handler);

        /// Reports how an action turned out. Called by the application once it has acted.
        void ReportActionOutcome(platform::ProcessActionResult const& result, std::wstring_view subject);

    private:
        void _buildLayout();

        /// Updates the summary line, which reflects the filter and is cheap.
        void _updateSummary();

        /**
         * @brief Creates an empty row.
         *
         * The row is a grid of text blocks in the fixed column order; values are
         * filled in by the binder, so the factory does not depend on which row it
         * will end up showing. That is what makes recycling possible.
         */
        [[nodiscard]] winrt::Microsoft::UI::Xaml::FrameworkElement _createRow();

        /// Writes one row's values from the current snapshot.
        void _bindRow(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, uint32_t rowIndex);

        /// Selects a process and repaints the rows to show it.
        void _selectRow(uint32_t pid);

        /// The selected process, or zero when none is.
        uint32_t m_selectedPid{0};

        /**
         * @brief One column's system-wide aggregate, retained so it can be updated in place.
         *
         * Held with the column it belongs to rather than by position, because the aggregate band skips
         * the columns that have no aggregate and the two lists would otherwise not line up.
         */
        struct AggregateCell
        {
            SortColumn column{SortColumn::Cpu};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock text{nullptr};
        };

        std::vector<AggregateCell> m_aggregates;

        /**
         * @brief One column's sort chevron, retained so it can be shown or hidden as the sort changes.
         *
         * Held with the column it belongs to rather than by position, for the same reason the aggregates
         * are: the two lists are built by separate loops and would otherwise not line up.
         */
        struct SortMark
        {
            SortColumn column{SortColumn::Name};
            winrt::Microsoft::UI::Xaml::Controls::FontIcon chevron{nullptr};
        };

        std::vector<SortMark> m_sortMarks;

        /// Shows the chevron on the sorted column, pointing the way the list is ordered, and hides it on
        /// every other.
        void _updateSortMarks();

        /// Writes the machine-wide figures into the aggregate band.
        void _updateHeaderAggregates();

        /**
         * @brief Icons for the rows, keyed by image name.
         *
         * Reading one costs a process open and a shell lookup, so nothing is read while a row is being
         * bound: the binder asks for what it has, and this drains a few queued reads per refresh. That is
         * what keeps the cost off the render path however many processes appear at once.
         */
        ProcessIconCache m_icons;

        /**
         * @brief The live width of each column, in effective pixels.
         *
         * Held here rather than read from the column table because the user can drag a column's edge, and
         * both the header and every row have to follow. The header and the rows are separate grids, so
         * there is nowhere else one width could live that both would see.
         */
        std::array<double, 7> m_columnWidths{};

        /// The header's column definitions, resized as a column is dragged.
        std::vector<winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition> m_headerColumns;

        /// Applies the current widths to the header and to every row on screen.
        void _applyColumnWidths();

        /// Resizes one column, applies it and reports the whole set.
        void _setColumnWidth(size_t column, double width);

        /// Called with the full width set after a drag, so the application can persist it.
        std::function<void(std::vector<double>)> m_onColumnWidthsChanged;

        /// Called when the selection changes.
        std::function<void(uint32_t)> m_onSelectionChanged;

        /// Called when the user asks to end a process. Empty until the application supplies one.
        std::function<platform::ProcessActionResult(uint32_t, bool)> m_onTerminate;

        /// Where an action's outcome is reported: the summary line, which is always visible.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_actionMessage{nullptr};

        /// Asks the user to confirm before ending anything, then calls the handler.
        void _confirmAndTerminate(uint32_t pid, winrt::hstring const& name, bool entireTree);

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_summary{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_emptyMessage{nullptr};

        std::unique_ptr<RowHost> m_rowHost;
        std::unique_ptr<ProcessListModel> m_listModel;

        /// The snapshot the rows are currently bound to. Held because binding needs
        /// the values; copying it is the expensive operation the version check in
        /// Refresh exists to avoid.
        domain::ProcessSnapshotView m_snapshot;

        ListQuery m_query;
        uint64_t m_renderedVersion{0};
    };
}
