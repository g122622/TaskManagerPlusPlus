#include "UI/Lists/RowHost.h"

#include "UI/Theming/Theme.h"

#include <algorithm>
#include <cmath>

using winrt::Microsoft::UI::Xaml::Controls::Canvas;
using winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility;
using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;

namespace tmpp::ui
{
    namespace
    {
        /// Rows built beyond the viewport on each side, so a fast scroll shows
        /// content immediately instead of flashing empty space.
        constexpr uint32_t OVERSCAN_ROWS = 4;

        /**
         * @brief Removes an element from the canvas by index.
         *
         * UIElementCollection offers RemoveAt rather than Remove, so the index has to
         * be found first. The collection is small (only the visible rows), so a linear
         * scan is not worth optimising.
         */
        void _detachFromCanvas(winrt::Microsoft::UI::Xaml::Controls::Canvas const& canvas,
                               winrt::Microsoft::UI::Xaml::FrameworkElement const& element)
        {
            uint32_t const count = canvas.Children().Size();
            for (uint32_t i = 0; i < count; ++i)
            {
                if (canvas.Children().GetAt(i) == element)
                {
                    canvas.Children().RemoveAt(i);
                    return;
                }
            }
        }
    }

    RowHost::RowHost(double rowHeight, double rowWidth) : m_rowHeight(rowHeight), m_rowWidth(rowWidth)
    {
        m_root = winrt::Microsoft::UI::Xaml::Controls::Grid();

        m_scroller = ScrollViewer();
        m_scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        m_scroller.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        m_scroller.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Stretch);

        // A Canvas gives absolute positioning and reports a definite size, which is
        // what lets the scroll extent be set from the row count alone.
        m_canvas = Canvas();
        m_canvas.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        m_scroller.Content(m_canvas);

        m_root.Children().Append(m_scroller);

        // Rebuilding on every scroll event would do redundant work during a flick;
        // ViewChanged fires after the offset settles enough to matter.
        m_scroller.ViewChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                      winrt::Microsoft::UI::Xaml::Controls::ScrollViewerViewChangedEventArgs const&) {
            _updateVisibleRows();
        });

        // A resize changes how many rows fit, so the range must be recomputed.
        m_scroller.SizeChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                      winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&) {
            m_hasRange = false;
            _updateVisibleRows();
        });
    }

    void RowHost::SetRowCount(uint32_t rowCount, RowFactory factory, RowBinder binder)
    {
        m_rowCount = rowCount;
        m_factory = std::move(factory);
        m_binder = std::move(binder);

        // The canvas is sized from the row count, which is what gives the scroller a
        // correct extent without creating any row visuals.
        m_canvas.Height(static_cast<double>(m_rowCount) * m_rowHeight);
        m_canvas.Width(m_rowWidth);

        // The row set changed, so every cached visual now belongs to a different row.
        // They are recycled rather than discarded, so the allocation is reused.
        for (auto& [index, element] : m_liveRows)
        {
            (void)index;
            _detachFromCanvas(m_canvas, element);
            m_recyclePool.push_back(element);
        }
        m_liveRows.clear();
        m_hasRange = false;

        _updateVisibleRows();
    }

    void RowHost::RefreshVisibleRows()
    {
        if (m_binder == nullptr)
        {
            return;
        }

        for (auto const& [index, element] : m_liveRows)
        {
            m_binder(element, index);
        }
    }

    void RowHost::ScrollToRow(uint32_t rowIndex)
    {
        if (rowIndex >= m_rowCount)
        {
            return;
        }

        double const target = static_cast<double>(rowIndex) * m_rowHeight;

        // Only scroll when the row is outside the current viewport, so selecting a
        // visible row does not yank the list.
        double const offset = _verticalOffset();
        double const viewport = _viewportHeight();
        if (target >= offset && (target + m_rowHeight) <= (offset + viewport))
        {
            return;
        }

        m_scroller.ChangeView(nullptr, target, nullptr, true);
    }

    double RowHost::_verticalOffset() const
    {
        return m_scroller.VerticalOffset();
    }

    double RowHost::_viewportHeight() const
    {
        double const height = m_scroller.ViewportHeight();
        // Before the first layout pass the viewport reports zero. Falling back to a
        // plausible height means the first rows are built immediately, rather than
        // the list appearing empty until the user scrolls.
        return (height > 1.0) ? height : (m_rowHeight * 20.0);
    }

    void RowHost::_updateVisibleRows()
    {
        if (m_rowCount == 0 || m_factory == nullptr)
        {
            return;
        }

        double const offset = _verticalOffset();
        double const viewport = _viewportHeight();

        auto const firstRaw = static_cast<int64_t>(std::floor(offset / m_rowHeight));
        // One extra row covers the partially visible row at the bottom edge.
        auto const visibleCount = static_cast<int64_t>(std::ceil(viewport / m_rowHeight)) + 1;

        auto const first = static_cast<uint32_t>(
            std::clamp<int64_t>(firstRaw - static_cast<int64_t>(OVERSCAN_ROWS), 0, m_rowCount));
        auto const lastExclusive =
            static_cast<uint32_t>(std::clamp<int64_t>(firstRaw + visibleCount + OVERSCAN_ROWS, 0, m_rowCount));

        // Nothing to do when the range is unchanged: this is what keeps a slow drag
        // from rebuilding rows on every event.
        if (m_hasRange && first == m_firstVisible && lastExclusive == m_lastVisible)
        {
            return;
        }

        m_firstVisible = first;
        m_lastVisible = lastExclusive;
        m_hasRange = true;

        // Retire rows that scrolled out of range, keeping their visuals for reuse.
        for (auto it = m_liveRows.begin(); it != m_liveRows.end();)
        {
            if (it->first < first || it->first >= lastExclusive)
            {
                _detachFromCanvas(m_canvas, it->second);
                m_recyclePool.push_back(it->second);
                it = m_liveRows.erase(it);
            }
            else
            {
                ++it;
            }
        }

        // Build or reuse a visual for every row now in range.
        for (uint32_t row = first; row < lastExclusive; ++row)
        {
            if (m_liveRows.contains(row))
            {
                continue;
            }

            winrt::Microsoft::UI::Xaml::FrameworkElement element{nullptr};
            if (!m_recyclePool.empty())
            {
                element = m_recyclePool.back();
                m_recyclePool.pop_back();
            }
            else
            {
                element = m_factory();
            }

            // Retired visuals were removed from the canvas when they scrolled out, so
            // a reused one must be added back. Appending unconditionally would throw
            // for one that is already a child, so the removal side and this side must
            // stay in step: retired means removed, reused means appended.
            m_canvas.Children().Append(element);

            Canvas::SetTop(element, static_cast<double>(row) * m_rowHeight);
            Canvas::SetLeft(element, 0.0);
            element.Width(m_rowWidth);
            element.Height(m_rowHeight);

            m_liveRows.emplace(row, element);
            m_binder(element, row);
        }
    }
}
