// A thin vertical bar that can be dragged to resize a table column.
//
// The bar is the affordance as well as the target. A boundary that is only a few transparent pixels wide
// is invisible until the user happens to find it, so this draws a hairline that brightens on hover and
// shows the horizontal resize cursor -- which is what says "this can be dragged" without a caption.
//
// It composes a plain Grid rather than deriving from one. Deriving would be the documented way to reach
// ProtectedCursor, but a derived XAML type needs an IDL-registered activation factory and this
// application has no IDL, so the compiler rejects it as abstract. The cursor is set through the interface
// directly instead; see ResizeCursor.h.
//
// The bar spans the full height of the table, not just the header. A boundary visible only while pointing
// at the header does not tell the user where the columns divide further down the list, which is where
// they are actually reading.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <functional>

namespace tmpp::ui
{
    /**
     * @brief A draggable boundary between two table columns.
     *
     * Owns its element and wires the pointer handling to it. Not thread-safe; the UI thread owns it.
     */
    class ColumnResizeHandle
    {
    public:
        ColumnResizeHandle();

        /// The element to place in the table's overlay.
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Element() const { return m_root; }

        /**
         * @brief Wires the drag.
         *
         * @param column Index of the column to the left of the boundary, which is the one a drag resizes.
         * @param widthOf Reads the column's current width, so a drag starts from where the column is now
         *        rather than from a value captured when the handle was built.
         * @param apply Called with the new width as the pointer moves.
         * @param onFinished Called once when the drag ends, so the caller can persist the result.
         */
        void Attach(uint32_t column,
                    std::function<double(uint32_t)> widthOf,
                    std::function<void(uint32_t, double)> apply,
                    std::function<void()> onFinished);

    private:
        void _onPointerEntered();
        void _onPointerExited();
        void _onPointerPressed(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void _onPointerMoved(winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args);
        void _onPointerReleased();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};

        uint32_t m_column{0};
        std::function<double(uint32_t)> m_widthOf;
        std::function<void(uint32_t, double)> m_apply;
        std::function<void()> m_onFinished;

        /// Where the pointer was when the drag began, and the width the column had then. Held as plain
        /// members because the handle outlives any single gesture.
        double m_startX{0.0};
        double m_startWidth{0.0};
        bool m_dragging{false};
    };
}
