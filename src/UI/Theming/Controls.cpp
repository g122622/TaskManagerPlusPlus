#include "UI/WinRTUI.h"
#include <winrt/Windows.UI.Text.h>

#include "UI/Theming/Controls.h"

namespace tmpp::ui::controls
{
    Media::Brush ThemedBrush(wchar_t const* key)
    {
        // Application::Current().Resources() resolves theme dictionaries, so a
        // "ThemeResource"-style lookup is done by hand here. A missing key yields a
        // transparent brush rather than an exception, so one absent resource cannot
        // take the window down.
        auto const resources = Application::Current().Resources();
        auto const found = resources.TryLookup(winrt::box_value(winrt::hstring{key}));
        if (found != nullptr)
        {
            if (auto const brush = found.try_as<Media::Brush>())
            {
                return brush;
            }
        }
        return Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent());
    }

    TextBlock MakeText(std::wstring_view text, double fontSize, bool subtle)
    {
        TextBlock block;
        block.Text(winrt::hstring{text});
        block.FontSize(fontSize);
        block.TextWrapping(TextWrapping::Wrap);
        if (subtle)
        {
            block.Opacity(0.7);
        }
        return block;
    }

    TextBlock MakeHeading(std::wstring_view text, double fontSize)
    {
        TextBlock block;
        block.Text(winrt::hstring{text});
        block.FontSize(fontSize);
        block.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        return block;
    }

    StackPanel MakeStack(double spacing)
    {
        StackPanel panel;
        panel.Spacing(spacing);
        return panel;
    }

    StackPanel MakeRow(double spacing)
    {
        StackPanel panel;
        panel.Spacing(spacing);
        panel.Orientation(Orientation::Horizontal);
        panel.VerticalAlignment(VerticalAlignment::Center);
        return panel;
    }

    Border MakeCard()
    {
        Border card;
        card.Padding(ThicknessHelper::FromUniformLength(16.0));
        card.CornerRadius(CornerRadiusHelper::FromUniformRadius(metrics::CARD_RADIUS));
        card.BorderThickness(ThicknessHelper::FromUniformLength(1.0));
        card.Background(ThemedBrush(theme::CARD_BACKGROUND));
        card.BorderBrush(ThemedBrush(theme::CARD_BORDER));
        return card;
    }

    Border MakeDivider()
    {
        Border line;
        line.Height(1.0);
        line.Background(ThemedBrush(theme::DIVIDER));
        line.HorizontalAlignment(HorizontalAlignment::Stretch);
        return line;
    }

    TextBox MakeSearchBox(std::wstring_view placeholder)
    {
        TextBox box;
        box.PlaceholderText(winrt::hstring{placeholder});
        box.MinWidth(240.0);
        return box;
    }

    void ApplyPageMargin(FrameworkElement const& element)
    {
        element.Margin(ThicknessHelper::FromLengths(metrics::PAGE_MARGIN, 16.0, metrics::PAGE_MARGIN, 12.0));
    }

    RowDefinition MakeAutoRow()
    {
        RowDefinition row;
        row.Height(GridLengthHelper::Auto());
        return row;
    }

    RowDefinition MakeStarRow()
    {
        RowDefinition row;
        row.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        return row;
    }

    RowDefinition MakeFixedRow(double height)
    {
        RowDefinition row;
        row.Height(GridLengthHelper::FromPixels(height));
        return row;
    }

    ColumnDefinition MakeAutoColumn()
    {
        ColumnDefinition column;
        column.Width(GridLengthHelper::Auto());
        return column;
    }

    ColumnDefinition MakeStarColumn()
    {
        ColumnDefinition column;
        column.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        return column;
    }

    ColumnDefinition MakeFixedColumn(double width)
    {
        ColumnDefinition column;
        column.Width(GridLengthHelper::FromPixels(width));
        return column;
    }
}
