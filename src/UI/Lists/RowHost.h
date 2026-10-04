// A virtualising row host.
//
// WinUI's ListView can populate rows through ContainerContentChanging, but that
// path depends on when the framework creates containers and applies templates, and
// it offers no way to reason about what is on screen. This host takes the simpler
// and fully deterministic approach: a Canvas of known total height inside a
// ScrollViewer, with row visuals created only for the rows currently in view and
// recycled as the user scrolls.
//
// The contract is deliberately narrow. Rows have a fixed height, which is what
// makes the visible range computable from the scroll offset alone -- no measuring,
// no layout pass, no per-row bookkeeping. That is the same assumption every
// virtualising list makes, stated explicitly here.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace tmpp::ui
{
    /**
     * @brief Shows a large number of fixed-height rows while building only the
     *        visible ones.
     *
     * Not thread-safe; the UI thread owns it.
     */
    class RowHost
    {
    public:
        /**
         * @brief Builds one row's visual tree.
         *
         * Called only when a row enters view and no recycled visual is available.
         * The returned element is cached and reused for other rows later.
         */
        using RowFactory = std::function<winrt::Microsoft::UI::Xaml::FrameworkElement()>;

        /**
         * @brief Fills in one row's values.
         *
         * Called whenever a row's data changes or it scrolls into view. The element
         * passed in is the one the factory produced.
         */
        using RowBinder = std::function<void(winrt::Microsoft::UI::Xaml::FrameworkElement const&, uint32_t rowIndex)>;

        /**
         * @param rowHeight Height of every row, in effective pixels.
         * @param rowWidth Width of every row; the host does not measure.
         */
        RowHost(double rowHeight, double rowWidth);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /**
         * @brief Sets the row count, rebuilding the visible rows.
         *
         * Call this when the underlying data changes size. The scroll position is
         * preserved where it still makes sense.
         */
        void SetRowCount(uint32_t rowCount, RowFactory factory, RowBinder binder);

        /**
         * @brief Sets the width every row is laid out at.
         *
         * The host does not measure, so it cannot derive this: a caller that changes its columns has to
         * tell it, or the rows would stay at the old width and the rightmost column would be clipped.
         */
        void SetRowWidth(double width);

        /// The row visuals currently alive, by row index.
        ///
        /// Exposed so a caller can update them in place when something they all share changes -- a column
        /// width, say -- rather than rebuilding the list and losing the scroll position.
        [[nodiscard]] std::unordered_map<uint32_t, winrt::Microsoft::UI::Xaml::FrameworkElement> const& LiveRows()
            const noexcept
        {
            return m_liveRows;
        }

        /**
         * @brief Re-binds every visible row without changing the row count.
         *
         * Used when values changed but the row set did not.
         */
        void RefreshVisibleRows();

        /// Scrolls the given row into view, if it is not already visible.
        void ScrollToRow(uint32_t rowIndex);

        /// Number of row visuals currently alive. Exposed for diagnostics and tests.
        [[nodiscard]] size_t LiveRowCount() const noexcept { return m_liveRows.size(); }

    private:
        /// Recomputes which rows should be visible and creates or recycles visuals.
        void _updateVisibleRows();

        /// Returns the scroller's vertical offset in pixels.
        [[nodiscard]] double _verticalOffset() const;

        /// Returns the scroller's viewport height in pixels.
        [[nodiscard]] double _viewportHeight() const;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_scroller{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Canvas m_canvas{nullptr};

        double m_rowHeight{28.0};
        double m_rowWidth{600.0};

        uint32_t m_rowCount{0};
        RowFactory m_factory;
        RowBinder m_binder;

        /// Row index to its visual, for the rows currently created. Entries outside
        /// the visible range are moved to the recycle pool rather than destroyed, so
        /// scrolling does not allocate.
        std::unordered_map<uint32_t, winrt::Microsoft::UI::Xaml::FrameworkElement> m_liveRows;

        /// Visuals no longer on screen, kept for reuse.
        std::vector<winrt::Microsoft::UI::Xaml::FrameworkElement> m_recyclePool;

        /// The range built last time, to avoid rebuilding on sub-pixel scrolls.
        uint32_t m_firstVisible{0};
        uint32_t m_lastVisible{0};
        bool m_hasRange{false};
    };
}
