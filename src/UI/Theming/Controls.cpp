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

    Grid MakeGrid(int32_t columns, int32_t rows, double columnSpacing, double rowSpacing)
    {
        Grid grid;
        for (int32_t i = 0; i < columns; ++i)
        {
            grid.ColumnDefinitions().Append(MakeStarColumn());
        }
        for (int32_t i = 0; i < rows; ++i)
        {
            grid.RowDefinitions().Append(MakeStarRow());
        }
        grid.ColumnSpacing(columnSpacing);
        grid.RowSpacing(rowSpacing);
        return grid;
    }

    Border MakeChartFrame(double radius)
    {
        Grid content;
        return MakeChartFrame(content, radius);
    }

    Border MakeChartFrame(Grid& outContent, double radius)
    {
        outContent = Grid();
        return MakeChartFrame(outContent.try_as<FrameworkElement>(), radius);
    }

    Border MakeChartFrame(FrameworkElement const& content, double radius)
    {
        // Every chart is outlined the same way, from this one place. Three separate implementations
        // had already drifted apart -- a translucent grey, the card stroke and a third colour --
        // while all three were meant to look identical.
        Border frame;
        frame.BorderThickness(ThicknessHelper::FromUniformLength(metrics::CHART_BORDER_THICKNESS));

        // The brush is dimmed rather than taken at full strength: the themed stroke is designed to
        // delineate a card, and on a chart it makes the frame the most prominent thing in the plot.
        auto const border = ThemedBrush(theme::CHART_BORDER);
        border.Opacity(metrics::CHART_BORDER_OPACITY);
        frame.BorderBrush(border);

        // One radius for every chart, so a grid of cells and a single large chart have matching
        // corners.
        frame.CornerRadius(CornerRadiusHelper::FromUniformRadius(radius));

        frame.Child(content);
        return frame;
    }

    ColumnDefinition MakeResizableColumn(Border& outHandle, std::function<void(double)> onResize, double initialWidth)
    {
        // WinUI has no GridSplitter, so the handle is a narrow Border with pointer handlers.
        //
        // The column's width follows the value the drag reports, so one place owns it and the column
        // cannot disagree with the persisted width.
        ColumnDefinition column;
        column.Width(GridLengthHelper::FromPixels(initialWidth));

        outHandle = Border();
        outHandle.Width(metrics::SPLITTER_WIDTH);
        outHandle.HorizontalAlignment(HorizontalAlignment::Right);

        // A transparent brush rather than none: a null Background is not hit-testable in WinUI, so
        // the handle would never receive the pointer.
        outHandle.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));

        // The handle only becomes visible while the pointer is over it, matching the original's
        // subtle divider.
        outHandle.PointerEntered(
            [handle = outHandle](winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) mutable {
                handle.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Color{0x40, 0x80, 0x80, 0x80}));
            });
        outHandle.PointerExited(
            [handle = outHandle](winrt::Windows::Foundation::IInspectable const&,
                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) mutable {
                handle.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            });

        // The drag is measured as movement from where it started rather than from the absolute
        // pointer position, so the boundary stays under the cursor wherever the drag begins.
        auto const startWidth = std::make_shared<double>(initialWidth);
        auto const startX = std::make_shared<double>(0.0);

        outHandle.PointerPressed([startX](winrt::Windows::Foundation::IInspectable const& sender,
                                          winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
            if (element == nullptr)
            {
                return;
            }

            *startX = args.GetCurrentPoint(element).Position().X;

            // Capture, because the drag leaves the narrow handle almost immediately.
            element.CapturePointer(args.Pointer());
        });

        outHandle.PointerMoved([startX, startWidth, onResize](
                                   winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
            if (element == nullptr || element.PointerCaptures().Size() == 0)
            {
                return;
            }

            double const delta = args.GetCurrentPoint(element).Position().X - *startX;
            if (onResize)
            {
                onResize(*startWidth + delta);
            }
        });

        outHandle.PointerReleased([startWidth, onResize](
                                      winrt::Windows::Foundation::IInspectable const& sender,
                                      winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
            if (element == nullptr)
            {
                return;
            }
            element.ReleasePointerCapture(args.Pointer());
            (void)startWidth;
            (void)onResize;
        });

        return column;
    }
}
