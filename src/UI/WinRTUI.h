// WinRT projections the UI layer needs.
//
// Included by every UI header rather than relying on a translation unit to have
// included the right projections first. The project has no precompiled header (see
// docs/BUILD.md), so each UI file must be self-sufficient: without this, a header
// that names a control type would only compile in translation units that happened
// to include the same projections earlier, which is a fragile and order-dependent
// arrangement.
#pragma once

#include <windows.h>
#include <unknwn.h>
#include <hstring.h>

// Windows defines GetCurrentTime as a macro, colliding with
// Microsoft::UI::Xaml::Media::Animation::Storyboard::GetCurrentTime.
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
