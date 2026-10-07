#include "UI/Lists/ColumnResizeHandle.h"

#include "UI/Theming/Controls.h"

using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
using winrt::Windows::UI::Color;

namespace tmpp::ui
{
    namespace
    {
        /// Thickness of the visible line. One pixel: it marks a boundary, and anything heavier competes
        /// with the column headings it sits between.
        constexpr double LINE_WIDTH = 1.0;

        /// How wide the grab area is. Wider than the line so the boundary can be hit without aiming, and
        /// narrow enough that it does not sit over the heading beside it.
        constexpr double HIT_WIDTH = 9.0;

        /// Faint enough to read as a separator rather than as content, and bright enough to be found
        /// without hunting. A boundary the user cannot see is the same as no boundary.
        ///
        /// A quarter opacity rather than the half it began at: seven boundaries across the table at half
        /// strength read as a ladder of bright rules competing with the columns, when all they have to do is
        /// mark where one ends and the next begins.
        constexpr Color LINE_IDLE{0x24, 0xFF, 0xFF, 0xFF};

        /// The colour while the pointer is over it, which is what confirms the bar is the target. This one
        /// is kept strong: it is a response to the pointer rather than part of the resting table, and it is
        /// only ever shown on one bar at a time.
        constexpr Color LINE_HOVER{0xA0, 0xFF, 0xFF, 0xFF};
    }

    ColumnResizeHandle::ColumnResizeHandle()
    {
        m_root = Grid();

        // The element is the line: a right-hand border thickness draws exactly one edge, which marks the
        // boundary without drawing a box around anything.
        m_root.Width(HIT_WIDTH);
        m_root.BorderThickness(winrt::Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0.0, 0.0, LINE_WIDTH, 0.0));
        m_root.BorderBrush(SolidColorBrush(LINE_IDLE));

        // Stretched vertically by the caller so the line runs the full height of the table.
        m_root.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Stretch);

        // A transparent background rather than none: a null Background is not hit-testable in WinUI, so a
        // handle without one would never receive the pointer.
        m_root.Background(SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));

        // The horizontal resize cursor, which is what tells the user the bar can be dragged. Set through
        // the interface rather than by deriving from a XAML type, which cannot be activated here.
        controls::SetResizeCursor(m_root);

        m_root.PointerEntered([this](winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const&) {
            _onPointerEntered();
        });

        m_root.PointerExited([this](winrt::Windows::Foundation::IInspectable const&, PointerRoutedEventArgs const&) {
            _onPointerExited();
        });

        m_root.PointerPressed([this](winrt::Windows::Foundation::IInspectable const&,
                                     PointerRoutedEventArgs const& args) { _onPointerPressed(args); });

        m_root.PointerMoved([this](winrt::Windows::Foundation::IInspectable const&,
                                   PointerRoutedEventArgs const& args) { _onPointerMoved(args); });

        m_root.PointerReleased([this](winrt::Windows::Foundation::IInspectable const&,
                                      PointerRoutedEventArgs const&) { _onPointerReleased(); });

        // A cancelled gesture -- the pointer leaving the window, say -- has to end the drag, or the line
        // would stay bright and the next move would resize without a press.
        m_root.PointerCaptureLost([this](winrt::Windows::Foundation::IInspectable const&,
                                         PointerRoutedEventArgs const&) { _onPointerReleased(); });
    }

    void ColumnResizeHandle::Attach(uint32_t column,
                                    std::function<double(uint32_t)> widthOf,
                                    std::function<void(uint32_t, double)> apply,
                                    std::function<void()> onFinished)
    {
        m_column = column;
        m_widthOf = std::move(widthOf);
        m_apply = std::move(apply);
        m_onFinished = std::move(onFinished);
    }

    void ColumnResizeHandle::_onPointerEntered()
    {
        m_root.BorderBrush(SolidColorBrush(LINE_HOVER));
    }

    void ColumnResizeHandle::_onPointerExited()
    {
        // Left bright while a drag is in progress: the pointer routinely leaves a nine pixel strip, and
        // the line going dim mid-drag would read as the gesture having been dropped.
        if (!m_dragging)
        {
            m_root.BorderBrush(SolidColorBrush(LINE_IDLE));
        }
    }

    void ColumnResizeHandle::_onPointerPressed(PointerRoutedEventArgs const& args)
    {
        // The width is read now rather than remembered from construction, so the drag starts from wherever
        // the column actually is -- which may not be where it was when this handle was built, since a
        // settings load or an earlier drag can have moved it.
        m_startWidth = m_widthOf ? m_widthOf(m_column) : 0.0;

        // Measured in the window's frame, not the handle's. The handle sits on the boundary it moves, so a
        // position measured from the handle is measured from a frame that moves with the drag: the
        // handle's own movement cancels the pointer's and the boundary tracks at half speed.
        auto const point = args.GetCurrentPoint(nullptr);
        if (point == nullptr)
        {
            return;
        }

        m_startX = point.Position().X;
        m_dragging = true;

        // Captured because the drag leaves the nine pixel strip almost immediately.
        m_root.CapturePointer(args.Pointer());
    }

    void ColumnResizeHandle::_onPointerMoved(PointerRoutedEventArgs const& args)
    {
        if (!m_dragging || !m_apply)
        {
            return;
        }

        auto const point = args.GetCurrentPoint(nullptr);
        if (point == nullptr)
        {
            return;
        }

        // Measured as movement from where the drag began rather than from the absolute pointer position,
        // so the boundary stays under the cursor wherever it was grabbed.
        m_apply(m_column, m_startWidth + (point.Position().X - m_startX));
    }

    void ColumnResizeHandle::_onPointerReleased()
    {
        if (!m_dragging)
        {
            return;
        }

        m_dragging = false;
        m_root.ReleasePointerCaptures();
        m_root.BorderBrush(SolidColorBrush(LINE_IDLE));

        // Reported once, at the end, rather than during the drag: persisting on every pointer move would
        // be a settings write per pixel.
        if (m_onFinished)
        {
            m_onFinished();
        }
    }
}
