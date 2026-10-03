// Shared WinUI helpers for building the interface in code.
#pragma once

#include "WinRT.h"

namespace tmpp
{
    // Fluent design uses a 4 px base grid.
    inline constexpr double BASE_UNIT = 4.0;

    /**
     * @brief Builds a placeholder page describing the M1-0 build-chain check.
     *
     * Replaced by the real Processes / Performance / Details pages in M1-4.
     */
    inline winrt::Microsoft::UI::Xaml::Controls::Grid MakePlaceholderPage()
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;

        Grid page;
        page.Padding(ThicknessHelper::FromLengths(24.0, 20.0, 24.0, 16.0));

        StackPanel stack;
        stack.Spacing(8.0);

        TextBlock title;
        title.Text(L"TaskManagerPlusPlus");
        title.FontSize(24.0);
        title.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

        TextBlock subtitle;
        subtitle.Text(L"M1-0 build chain verification (code-built UI)");
        subtitle.Opacity(0.7);

        Border card;
        card.Padding(ThicknessHelper::FromLengths(16.0, 16.0, 16.0, 16.0));
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(8.0));
        card.BorderThickness(ThicknessHelper::FromUniformLength(1.0));
        card.Margin(ThicknessHelper::FromLengths(0.0, 8.0, 0.0, 0.0));

        StackPanel cardContent;
        cardContent.Spacing(8.0);

        TextBlock cardTitle;
        cardTitle.Text(L"What this verifies");
        cardTitle.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

        TextBlock cardBody;
        cardBody.TextWrapping(TextWrapping::Wrap);
        cardBody.Text(
            L"If this window is visible, then NuGet restore, the C++/WinRT projection, the MSVC build, "
            L"the WinUI 3 runtime and self-contained deployment all work. The UI is composed in C++ "
            L"because this Visual Studio installation has no native C++ XAML build support.");

        cardContent.Children().Append(cardTitle);
        cardContent.Children().Append(cardBody);
        card.Child(cardContent);

        stack.Children().Append(title);
        stack.Children().Append(subtitle);
        stack.Children().Append(card);
        page.Children().Append(stack);

        return page;
    }
}
