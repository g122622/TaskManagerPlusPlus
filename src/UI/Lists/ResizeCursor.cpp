#include "UI/Lists/ResizeCursor.h"

#include <inspectable.h>

#include <winrt/Microsoft.UI.Input.h>

// The generated ABI declarations. The consuming projection is already in the shared header, but the
// abi<> specialisations live in the per-namespace headers that the projection includes for its own use.
// Including this one is what makes abi<IUIElementProtected> visible, so the call below is a normal
// virtual call against the shape the projection generated rather than an index into a table.
#include <winrt/impl/Microsoft.UI.Xaml.0.h>

namespace tmpp::ui::controls
{
    void SetResizeCursor(winrt::Microsoft::UI::Xaml::UIElement const& element)
    {
        if (element == nullptr)
        {
            return;
        }

        // Created once and shared: InputSystemCursor::Create returns a cached instance, but calling it per
        // handle would still be a call per handle for a value that never varies.
        static winrt::Microsoft::UI::Input::InputCursor const resizeCursor =
            winrt::Microsoft::UI::Input::InputSystemCursor::Create(
                winrt::Microsoft::UI::Input::InputSystemCursorShape::SizeWestEast);

        auto const unknown = element.as<::IUnknown>();

        winrt::com_ptr<::IUnknown> queried;
        if (FAILED(unknown->QueryInterface(winrt::guid_of<winrt::Microsoft::UI::Xaml::IUIElementProtected>(),
                                           queried.put_void())))
        {
            // A failed query leaves the default arrow rather than failing anything: the cursor is an
            // affordance, and the bar is draggable without it.
            return;
        }

        using Abi = winrt::impl::abi<winrt::Microsoft::UI::Xaml::IUIElementProtected>::type;

        auto* const access = reinterpret_cast<Abi*>(queried.get());
        access->put_ProtectedCursor(winrt::get_abi(resizeCursor));
    }
}
