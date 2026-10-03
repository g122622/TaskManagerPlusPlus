// Builders for the WinUI control tree.
//
// Every control in this application is created in C++ rather than declared in
// XAML, because this Visual Studio installation has no native C++ XAML build
// support (see docs/BUILD.md). That makes the construction code the equivalent of
// a XAML file, so these helpers exist to keep it readable: a fluent setter chain
// per control instead of a wall of property assignments.
#pragma once

#include "UI/WinRTUI.h"


#include <string_view>

#include "UI/Theme.h"

namespace tmpp::ui
{
    namespace controls
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;

        /**
         * @brief Looks up a themed brush by resource key.
         *
         * Falls back to a transparent brush when the key is missing, which keeps a
         * missing resource from taking the window down.
         */
        [[nodiscard]] Media::Brush ThemedBrush(wchar_t const* key);

        /**
         * @brief Creates a text block.
         *
         * @param text Content.
         * @param fontSize Size in effective pixels.
         * @param subtle When true, uses the secondary text colour, which is what
         *        Task Manager uses for captions and secondary values.
         */
        [[nodiscard]] TextBlock MakeText(std::wstring_view text, double fontSize = 14.0, bool subtle = false);

        /**
         * @brief Creates a bold section heading.
         */
        [[nodiscard]] TextBlock MakeHeading(std::wstring_view text, double fontSize = 20.0);

        /**
         * @brief Creates a vertical stack with the standard spacing.
         */
        [[nodiscard]] StackPanel MakeStack(double spacing = metrics::STACK_SPACING);

        /**
         * @brief Creates a horizontal stack with the standard spacing.
         */
        [[nodiscard]] StackPanel MakeRow(double spacing = metrics::STACK_SPACING);

        /**
         * @brief Creates a card: a bordered, rounded surface holding one group.
         *
         * This is the building block of the performance page's hardware panels.
         */
        [[nodiscard]] Border MakeCard();

        /**
         * @brief Creates a full-width horizontal divider.
         */
        [[nodiscard]] Border MakeDivider();

        /**
         * @brief Creates a button that invokes a plain callable.
         *
         * The Click handler is attached here so callers do not repeat the
         * delegate construction, which is verbose and easy to get wrong.
         */
        template <typename TCallback>
        [[nodiscard]] Button MakeButton(std::wstring_view caption, TCallback&& onClick)
        {
            Button button;
            button.Content(winrt::box_value(winrt::hstring{caption}));
            button.Click([callback = std::forward<TCallback>(onClick)](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
                callback();
            });
            return button;
        }

        /**
         * @brief Creates a search box with a placeholder.
         */
        [[nodiscard]] TextBox MakeSearchBox(std::wstring_view placeholder);

        /**
         * @brief Applies the standard page padding to a panel.
         */
        void ApplyPageMargin(FrameworkElement const& element);
    }
}
