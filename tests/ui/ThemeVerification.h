#pragma once

#include "UI/WinRTUI.h"

namespace tmpp::ui::test
{
    void EnableThemeVerification();
    bool ThemeVerificationEnabled();
    int ThemeVerificationExitCode();
    winrt::Microsoft::UI::Xaml::Window StartThemeVerification();
    void EnableWindowVerification();
    bool WindowVerificationEnabled();
    int WindowVerificationExitCode();
    winrt::Microsoft::UI::Xaml::Window StartWindowVerification();
}
