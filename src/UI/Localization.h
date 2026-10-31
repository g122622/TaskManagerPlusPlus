#pragma once

#include "Core/Localization.h"
#include "UI/WinRTUI.h"

namespace tmpp::ui {
inline winrt::hstring Localize(std::wstring_view text)
{
    return winrt::hstring{core::localization::Translate(text)};
}
inline winrt::hstring Localize(std::string_view text)
{
    return winrt::to_hstring(core::localization::Translate(text));
}
inline winrt::hstring Localize(std::wstring const& text)
{
    return Localize(std::wstring_view{text});
}
inline winrt::hstring Localize(std::string const& text)
{
    return Localize(std::string_view{text});
}
inline winrt::hstring Localize(winrt::hstring const& text)
{
    return Localize(std::wstring_view{text});
}
inline winrt::hstring Localize(wchar_t const* text)
{
    return Localize(std::wstring_view{text});
}
inline winrt::hstring Localize(char const* text)
{
    return Localize(std::string_view{text});
}
template <typename T>
    requires std::is_arithmetic_v<T>
winrt::hstring Localize(T value)
{
    return winrt::to_hstring(value);
}

void LocalizeTree(winrt::Microsoft::UI::Xaml::DependencyObject const& root);
[[nodiscard]] bool LocalizationInProgress();
} // namespace tmpp::ui
