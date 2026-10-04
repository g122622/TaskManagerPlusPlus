// The resize cursor for a table column boundary.
//
// WinUI exposes the cursor through ProtectedCursor, which is a protected member of UIElement: it has no
// public setter, and the documented way to reach it is to derive from a XAML element. Deriving is not
// available to this project -- a derived XAML type needs an IDL-registered activation factory, and this
// application has no IDL, so the compiler rejects the class as abstract.
//
// The interface that carries the property is IUIElementProtected, however, and a plain element can be
// queried for it directly. That is what this does: the same property through the same interface, reached
// by a query rather than by inheritance.
#pragma once

#include "UI/WinRTUI.h"

namespace tmpp::ui
{
    namespace controls
    {
        /**
         * @brief Sets the cursor shown while the pointer is over an element.
         *
         * Does nothing when the interface is unavailable, which leaves the default arrow rather than
         * failing: the cursor is an affordance, and a missing one is a worse experience than a crash but
         * a much better one than refusing to build the handle at all.
         *
         * @param element The element to set it on. It has to be a XAML element with a cursor, which every
         *        UIElement is.
         */
        void SetResizeCursor(winrt::Microsoft::UI::Xaml::UIElement const& element);
    }
}
