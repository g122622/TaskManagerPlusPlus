// Shared Windows and C++/WinRT projection includes.
//
// NOTE: This is an ordinary header, not a precompiled header. The project builds
// without one: the WinUI projections are template-heavy, and a precompiled
// header large enough to cover them reached the compiler's internal heap limit
// (C1076). Each translation unit therefore parses these includes directly.
#pragma once

#include <windows.h>
#include <unknwn.h>
#include <hstring.h>

// Windows defines GetCurrentTime as a macro, which collides with
// Microsoft::UI::Xaml::Media::Animation::Storyboard::GetCurrentTime().
#undef GetCurrentTime

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>