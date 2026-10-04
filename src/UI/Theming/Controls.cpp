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

            // GetCurrentPoint returns null when the pointer has no position relative to the element, which
            // happens as it leaves or the element is removed mid-gesture. Calling Position() on it
            // dereferences null, so every use is guarded.
            auto const point = args.GetCurrentPoint(element);
            if (point == nullptr)
            {
                return;
            }

            *startX = point.Position().X;

            // Capture, because the drag leaves the narrow handle almost immediately.
            element.CapturePointer(args.Pointer());
        });

        outHandle.PointerMoved([startX, startWidth, onResize](
                                   winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
            if (element == nullptr)
            {
                return;
            }

            // PointerCaptures() is only non-null once the pointer system has given the element a capture
            // collection; calling Size() before that dereferences null.
            auto const captures = element.PointerCaptures();
            if (captures == nullptr || captures.Size() == 0)
            {
                return;
            }

            auto const point = args.GetCurrentPoint(element);
            if (point == nullptr)
            {
                return;
            }

            double const delta = point.Position().X - *startX;
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

    void MakeWindowDragRegion(
        winrt::Microsoft::UI::Xaml::UIElement const& element,
        HWND windowHandle,
        std::function<bool(winrt::Windows::Foundation::Point const&)> shouldDrag,
        bool includeHandled)
    {
        if (element == nullptr || windowHandle == nullptr)
        {
            return;
        }

        // Where the current press began, and whether it is still a candidate for a drag. Held per
        // gesture and reset on release, so consecutive drags each start from their own press.
        struct Gesture
        {
            winrt::Windows::Foundation::Point origin{0.0, 0.0};
            bool armed{false};
        };
        auto const gesture = std::make_shared<Gesture>();

        // The press arms the gesture; the drag starts once the pointer has travelled far enough. That
        // threshold is what keeps a click and a drag apart on a surface of buttons: a press that does not
        // move is left entirely alone, so the button underneath still receives both its press and its
        // release and fires its Click.
        constexpr double DRAG_THRESHOLD = 4.0;

        auto const onPressed = [gesture, shouldDrag](
                                   winrt::Windows::Foundation::IInspectable const& sender,
                                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            gesture->armed = false;

            auto const point = args.GetCurrentPoint(nullptr);
            if (point == nullptr || !point.Properties().IsLeftButtonPressed())
            {
                return;
            }

            auto const host = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
            if (host == nullptr)
            {
                return;
            }

            if (shouldDrag)
            {
                // The test is given a position relative to the element rather than to the window, so a
                // caller can express "the rail along the left" without knowing where the element sits on
                // screen.
                auto const local = args.GetCurrentPoint(host);
                if (local == nullptr || !shouldDrag(local.Position()))
                {
                    return;
                }
            }

            auto const local = args.GetCurrentPoint(host);
            gesture->origin = (local != nullptr) ? local.Position() : winrt::Windows::Foundation::Point{0.0, 0.0};
            gesture->armed = true;

            // The pointer is deliberately not captured.
            //
            // Capturing it here is what made the gesture need a click-and-release before it would work:
            // the capture took the pointer away from the button and the ScrollViewer underneath, so the
            // press was consumed by the drag logic and the element never saw the follow-up events that
            // would normally have ended its own gesture. The capture is also unnecessary, because sending
            // the non-client hit test below hands the drag to the window manager, which captures the
            // pointer itself. Until then the pointer stays over the window, so the moves keep arriving
            // here without a capture.
            (void)point;
        };

        auto const onMoved = [gesture, windowHandle](
                                 winrt::Windows::Foundation::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
            if (!gesture->armed)
            {
                return;
            }

            auto const host = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
            if (host == nullptr)
            {
                return;
            }

            auto const local = args.GetCurrentPoint(host);
            if (local == nullptr)
            {
                return;
            }

            double const dx = local.Position().X - gesture->origin.X;
            double const dy = local.Position().Y - gesture->origin.Y;
            if ((dx * dx) + (dy * dy) < DRAG_THRESHOLD * DRAG_THRESHOLD)
            {
                return;
            }

            gesture->armed = false;

            // The move is delegated to the window manager rather than computed from pointer deltas. Those
            // deltas would cover the basic drag, but the system's own handling also gives snapping, the
            // double-click-to-maximise gesture and correct behaviour when the drag crosses to another
            // monitor; reproducing them from positions would be a worse copy of something already
            // available.
            //
            // Releasing the capture first is what lets the window manager take the pointer, and the
            // non-client hit test is what tells it this is a caption drag rather than a client one.
            ::ReleaseCapture();
            ::SendMessageW(windowHandle, WM_NCLBUTTONDOWN, HTCAPTION, 0);

            args.Handled(true);
        };

        auto const onReleased = [gesture](winrt::Windows::Foundation::IInspectable const&,
                                          winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            // The gesture ends whether or not it became a drag, so the next press starts clean.
            gesture->armed = false;
        };

        // The events are observed through AddHandler when handled presses are wanted, and through the
        // plain accessors otherwise.
        //
        // All of them are attached the same way, which is the part that was wrong before: only the press
        // was attached for handled events, while the move and release used the plain accessors. A surface
        // containing a ScrollViewer has its moves handled by that control, so the press armed the gesture
        // and the move that should have started the drag never arrived -- leaving the gesture to be
        // retried, which is what presented as needing a click first.
        auto const attach = [includeHandled, element](
                                winrt::Microsoft::UI::Xaml::RoutedEvent const& routedEvent,
                                winrt::Microsoft::UI::Xaml::Input::PointerEventHandler const& handler) {
            element.AddHandler(routedEvent, winrt::box_value(handler), includeHandled);
        };

        attach(winrt::Microsoft::UI::Xaml::UIElement::PointerPressedEvent(), onPressed);
        attach(winrt::Microsoft::UI::Xaml::UIElement::PointerMovedEvent(), onMoved);
        attach(winrt::Microsoft::UI::Xaml::UIElement::PointerReleasedEvent(), onReleased);
        attach(winrt::Microsoft::UI::Xaml::UIElement::PointerCaptureLostEvent(), onReleased);
        attach(winrt::Microsoft::UI::Xaml::UIElement::PointerCanceledEvent(), onReleased);
    }
}
