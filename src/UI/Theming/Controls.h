// Builders for the WinUI control tree.
//
// Every control in this application is created in C++ rather than declared in
// XAML, because this Visual Studio installation has no native C++ XAML build
// support (see docs/BUILD.md). That makes the construction code the equivalent of
// a XAML file, so these helpers exist to keep it readable: a fluent setter chain
// per control instead of a wall of property assignments.
#pragma once

#include "UI/WinRTUI.h"


#include <functional>
#include <string_view>

#include "UI/Theming/Theme.h"

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

        // --- Grid sizing helpers ------------------------------------------------
        //
        // These exist because the default is a trap: GridLength defaults to 1* (Star),
        // not Auto, so a default-constructed RowDefinition silently takes an equal share
        // of the available height. Appending two defaults where one was meant to be
        // content-sized splits the space in half regardless of what the content measures,
        // which is what left a chart canvas with 16 of its cell's 33 pixels and made every
        // core chart decline to draw.
        //
        // Naming the intent removes the possibility of getting it wrong by omission.

        /// A grid row sized to its content.
        [[nodiscard]] RowDefinition MakeAutoRow();

        /// A grid row that absorbs the remaining height.
        [[nodiscard]] RowDefinition MakeStarRow();

        /// A grid row of a fixed height in effective pixels.
        [[nodiscard]] RowDefinition MakeFixedRow(double height);

        /// A grid column sized to its content.
        [[nodiscard]] ColumnDefinition MakeAutoColumn();

        /// A grid column that absorbs the remaining width.
        [[nodiscard]] ColumnDefinition MakeStarColumn();

        /// A grid column of a fixed width in effective pixels.
        [[nodiscard]] ColumnDefinition MakeFixedColumn(double width);

        /**
         * @brief Creates an evenly divided grid.
         *
         * @param columns Number of star columns.
         * @param rows Number of star rows.
         * @param columnSpacing Gap between columns.
         * @param rowSpacing Gap between rows.
         */
        [[nodiscard]] Grid MakeGrid(int32_t columns, int32_t rows, double columnSpacing = 0.0, double rowSpacing = 0.0);

        /**
         * @brief Creates the outlined frame every chart sits in.
         *
         * One factory rather than an outline configured at each call site, because three
         * separate implementations had already drifted apart: the per-core cells drew a
         * translucent grey, the memory chart drew the card stroke, and the composition strip drew
         * its own. They were meant to look identical and did not.
         *
         * The frame is returned empty; the caller sets its child. The outline is always on, so
         * no caller can forget to switch it on, and the charts cannot disagree about it.
         *
         * @param radius Corner radius. The default suits a chart; a thin strip wants a smaller
         *        one so its ends do not read as pill-shaped.
         */
        [[nodiscard]] Border MakeChartFrame(double radius = metrics::CHART_CORNER_RADIUS);

        /**
         * @brief Creates a chart frame around caller-supplied content.
         *
         * @param content What the outline is drawn around.
         * @param radius Corner radius.
         */
        [[nodiscard]] Border MakeChartFrame(FrameworkElement const& content,
                                            double radius = metrics::CHART_CORNER_RADIUS);

        /**
         * @brief Creates a chart frame and hands back an empty grid to place content in.
         *
         * The convenience form for the common case, where the outline wraps a host the caller
         * then fills.
         *
         * @param outContent Receives the empty grid inside the frame.
         * @param radius Corner radius.
         */
        [[nodiscard]] Border MakeChartFrame(Grid& outContent, double radius = metrics::CHART_CORNER_RADIUS);

        /**
         * @brief Sets the cursor shown while the pointer is over an element.
         *
         * WinUI exposes the cursor through ProtectedCursor, which is a protected member of UIElement: it
         * has no public setter, and the documented way to reach it is to derive from a XAML element.
         * Deriving is not available to this project -- a derived XAML type needs an IDL-registered
         * activation factory, and this application has no IDL, so the compiler rejects the class as
         * abstract. The interface that carries the property is IUIElementProtected, however, and a plain
         * element can be queried for it directly: the same property through the same interface, reached
         * by a query rather than by inheritance.
         *
         * Does nothing when the interface is unavailable, which leaves the default arrow rather than
         * failing: the cursor is an affordance, and a missing one is a worse experience than a crash but
         * a much better one than refusing to build the handle at all.
         *
         * @param element The element to set it on. It has to be a XAML element with a cursor, which every
         *        UIElement is.
         */
        void SetResizeCursor(winrt::Microsoft::UI::Xaml::UIElement const& element);

        /**
         * @brief Creates a grid column whose width can be dragged.
         *
         * WinUI has no GridSplitter, so the handle is a thin transparent Border carrying the pointer
         * handlers. The drag is reported through a callback rather than applied to the column from here:
         * the caller owns both the column and the persisted width, and two places setting the width is
         * how they drift apart.
         *
         * The handle draws the horizontal resize cursor and a grey wash while the pointer is over it.
         * Both are the whole affordance: a strip that is invisible at rest and only reacts once the
         * pointer is exactly on it is found by accident, if at all.
         *
         * @param outHandle Receives the handle. The caller gives it a width -- SPLITTER_WIDTH is the usual
         *        -- and places it so that it lies along the boundary it moves. It is stretched down the
         *        side already. Where it sits is the caller's decision because that is what decides whether
         *        the grab area takes space from the layout or fits inside it.
         * @param currentWidth Reads the pane's width as it is now. Read when a drag begins rather than
         *        remembered from construction, so a second drag starts from where the first ended.
         * @param onResize Called with the new width as the handle is dragged.
         * @param initialWidth Width the column is created with.
         */
        [[nodiscard]] ColumnDefinition MakeResizableColumn(Border& outHandle,
                                                           std::function<double()> currentWidth,
                                                           std::function<void(double)> onResize,
                                                           double initialWidth);

        /**
         * @brief Makes an element a region the user can drag the window by.
         *
         * A window whose title bar is not laid out by the system has no drag region unless one is
         * nominated, so a compact window with no visible title bar cannot be moved at all.
         *
         * The drag is handed to the window manager rather than being simulated from pointer deltas:
         * Windows then also gives the native behaviour for free -- snapping, the double-click-to-maximise
         * gesture, and correct handling of a drag that crosses to another monitor.
         *
         * Only a press the element itself receives starts a drag. A control that handles the press --
         * every button does -- stops it before this handler runs, so a draggable surface containing
         * buttons still has working buttons.
         *
         * @param element The region to make draggable.
         * @param windowHandle The top-level window to move.
         * @param shouldDrag Optional test of the press position, for a region that is only part of the
         *        element. A null test makes the whole element draggable.
         * @param includeHandled When true, the drag also starts on a press a child control consumed. A
         *        button consumes its press, so a surface made of buttons is only draggable by its gaps
         *        unless this is set; it is off by default because it takes the gesture away from the
         *        child.
         */
        void MakeWindowDragRegion(
            winrt::Microsoft::UI::Xaml::UIElement const& element,
            HWND windowHandle,
            std::function<bool(winrt::Windows::Foundation::Point const&)> shouldDrag = {},
            bool includeHandled = false);
    }
}
