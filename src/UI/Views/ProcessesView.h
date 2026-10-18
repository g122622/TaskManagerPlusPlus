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
#include "UI/Controls/ProcessDetailPanel.h"
#include "UI/Lists/ColumnResizeHandle.h"
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

        // ====================================================================
        // Detail sidebar
        // ====================================================================
        //
        // The sidebar shows and changes one process's settings. Its width and whether it is shown at
        // all are the user's, so both are reported rather than written: the application owns the
        // settings file and decides when to save, exactly as it does for the column widths.

        /**
         * @brief Adopts the remembered sidebar width and visibility.
         *
         * @param width Width in effective pixels, or a non-positive value to keep the default.
         * @param visible Whether the sidebar is shown.
         */
        void SetSidebarLayout(double width, bool visible);

        /**
         * @brief Called when the user resizes or shows or hides the sidebar.
         *
         * Both values are reported together because they are stored together and a partial report
         * would let the file hold a width for a pane the user has hidden, or a visibility for a width
         * they no longer have.
         */
        void SetSidebarLayoutHandler(std::function<void(double, bool)> handler);

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

        /**
         * @brief Rebinds the one row showing a given process, if it is on screen.
         *
         * The selection highlight moves between two rows; rebinding the other rows to redraw it is work
         * that produces no visible change, so only the rows that changed are rebound.
         */
        void _refreshRowForPid(uint32_t pid);

        /// Builds the toolbar's right-hand end: the switch that shows and hides the sidebar.
        void _buildSidebarToggle();

        /// Resizes the sidebar, clamped, and reports the new layout.
        void _setSidebarWidth(double width);

        /// Shows or hides the sidebar and reports the new layout.
        void _setSidebarVisible(bool visible);

        /// Applies the remembered width and visibility to the column and the panel.
        void _applySidebarLayout();

        /// Writes the current sample's values into the sidebar, or clears it when the process has
        /// exited. Called from Refresh, which is the only place the sample is known to be current.
        void _updateSidebar();

        /// Reports what the rule engine did since the last refresh, if anything.
        void _reportRuleEvents();

        /// The selected process, or zero when none is.
        uint32_t m_selectedPid{0};

        /**
         * @brief Whether a row is selected at all.
         *
         * A separate flag because zero is a real process id: the System Idle Process is pid 0, and
         * without this, clicking its row set the same value that means "nothing is selected" and the
         * row appeared not to respond. The consumers that take a pid -- the details page, the settings
         * file -- still see 0, because neither has anything to show for a process with no executable
         * image behind it; this is only about the list's own highlight and the sidebar.
         */
        bool m_selectionActive{false};

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

        /**
         * @brief The layer the column boundaries are drawn in, over both the header and the rows.
         *
         * A Canvas rather than a grid of columns, because the bars are positioned from the widths to their
         * left and are not part of the table's own layout: they take no space and the list underneath
         * still receives the pointer everywhere except on a bar.
         */
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_handleLayer{nullptr};

        /// One bar per boundary, in column order. The last column has none.
        std::vector<std::unique_ptr<ColumnResizeHandle>> m_columnHandles;

        /// Applies the current widths to the header and to every row on screen.
        void _applyColumnWidths();

        // ====================================================================
        // Cell shading
        // ====================================================================
        //
        // Each metric cell is filled from transparent up to a single deep blue, in proportion to the
        // value it holds. It is the same device Windows' own task manager uses, and the reason it earns
        // its place is that a column of numbers has to be read one at a time while a column of bands
        // can be taken in at a glance: which rows are heavy, and how far apart they are, is legible
        // before any figure is.

        /**
         * @brief The largest value each metric column currently shows.
         *
         * Rebuilt from the visible rows on every new sample. Relative shading rather than a fixed
         * scale, which is what makes the busiest row of a column reach full strength: on an idle
         * machine every band would otherwise be indistinguishable from no band at all, and the same
         * depth would mean something different in each column.
         */
        std::array<double, 7> m_columnMaxima{};

        /// Recomputes @c m_columnMaxima from the rows the list is showing.
        void _updateColumnMaxima();

        /**
         * @brief The fill for one metric cell.
         *
         * @param column Column index in the column table.
         * @param value The value the cell holds, or a negative number when there is nothing to shade.
         */
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Media::Brush _heatBrush(size_t column, double value) const;

        /**
         * @brief Pre-built fills, one per shading step.
         *
         * Built once rather than per cell: a full rebind touches every metric cell of every row on
         * screen, and allocating a brush for each of those would put hundreds of allocations on a path
         * that runs whenever the list is resorted (docs/ARCHITECTURE.md, constraint P-004).
         *
         * The steps are quantised, which also means rows with nearly equal values get the same brush and
         * stop flickering between two imperceptibly different shades as the values move.
         */
        std::vector<winrt::Microsoft::UI::Xaml::Media::SolidColorBrush> m_heatBrushes;

        /// The fill for a cell with nothing to shade.
        ///
        /// Held rather than made on demand because it is handed out for every unshaded cell of every
        /// row on every rebind, and constructing a brush for each of those puts dozens of allocations on
        /// a path that runs whenever the list is resorted (docs/ARCHITECTURE.md, constraint P-004).
        winrt::Microsoft::UI::Xaml::Media::SolidColorBrush m_transparentFill{nullptr};

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

        // --- Detail sidebar ---------------------------------------------------

        /// The sidebar. Owned here rather than by the window because it is bound to this list's
        /// selection and has no meaning without it.
        std::unique_ptr<ProcessDetailPanel> m_detailPanel;

        /// The identity of the process the sidebar is showing, so a recycled pid is detected rather
        /// than silently inheriting the previous process's panel state.
        uint64_t m_selectedCreateTime{0};

        /// The sidebar's column, whose width the drag handle changes.
        winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition m_detailColumn{nullptr};

        /// The drag handle, laid along the sidebar's left edge.
        winrt::Microsoft::UI::Xaml::Controls::Border m_detailSplitter{nullptr};

        /// The card the sidebar's content sits in, so hiding the pane can collapse the card as well
        /// as the column: a column of zero width still draws a card border one pixel wide.
        winrt::Microsoft::UI::Xaml::Controls::Border m_detailCard{nullptr};

        /// The toolbar switch that shows and hides the sidebar.
        winrt::Microsoft::UI::Xaml::Controls::Primitives::ToggleButton m_detailToggle{nullptr};

        /// The sidebar's live width, in effective pixels.
        double m_detailWidth{0.0};

        /// The width the sidebar had when the current drag began.
        ///
        /// Kept because the sidebar is on the right, where the pane grows as the pointer moves left:
        /// the drag handle reports a value that increases with the pointer's own movement, so the
        /// change has to be measured against the width the drag started from rather than read as the
        /// new width. The handle reads the width through the accessor below exactly once, when the
        /// press lands, which is where this is set.
        double m_detailWidthAtDragStart{0.0};

        /// Whether the sidebar is shown.
        bool m_detailVisible{true};

        /// Called with the width and visibility after a drag or a toggle.
        std::function<void(double, bool)> m_onSidebarLayoutChanged;
    };
}
