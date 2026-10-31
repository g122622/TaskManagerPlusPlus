#pragma once

#include "UI/WinRTUI.h"

namespace tmpp::ui
{
    class AboutPage
    {
    public:
        AboutPage();
        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }
    private:
        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
    };
}
