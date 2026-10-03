// Shared UI styling constants.
//
// Values live here rather than inline so the spacing and colours stay consistent
// between the shell, the process list and the performance page, and so matching
// the Windows 11 Task Manager's look is a matter of changing numbers in one place.
#pragma once

#include "UI/WinRTUI.h"


namespace tmpp::ui
{
    namespace metrics
    {
        /// Fluent design's base grid. Every margin and padding is a multiple.
        inline constexpr double BASE_UNIT = 4.0;

        /// Outer page margin.
        inline constexpr double PAGE_MARGIN = 24.0;

        /// Spacing between stacked elements.
        inline constexpr double STACK_SPACING = 8.0;

        /// Corner radius for cards.
        inline constexpr double CARD_RADIUS = 8.0;

        /// Corner radius for controls.
        inline constexpr double CONTROL_RADIUS = 4.0;

        /// Per-row height in the process list. Fixed so row virtualisation can
        /// reason about scroll offsets without measuring every row.
        inline constexpr double PROCESS_ROW_HEIGHT = 28.0;

        /// Width of the navigation pane, matching Task Manager's compact rail.
        inline constexpr double NAVIGATION_PANE_WIDTH = 240.0;

        /// Height reserved for the status bar.
        inline constexpr double STATUS_BAR_HEIGHT = 28.0;
    }

    namespace theme
    {
        /**
         * @brief The resource key for a themed brush.
         *
         * WinUI resolves these through XamlControlsResources, which the application
         * merges at startup; using the keys rather than hard-coded colours is what
         * makes light, dark and high-contrast themes work without extra code.
         */
        inline constexpr wchar_t const* PAGE_BACKGROUND = L"ApplicationPageBackgroundThemeBrush";
        inline constexpr wchar_t const* CARD_BACKGROUND = L"CardBackgroundFillColorDefaultBrush";
        inline constexpr wchar_t const* CARD_BORDER = L"CardStrokeColorDefaultBrush";
        inline constexpr wchar_t const* CARD_BORDER_ALT = L"CardStrokeColorDefaultBrush";
        inline constexpr wchar_t const* SUBTLE_TEXT = L"TextFillColorSecondaryBrush";
        inline constexpr wchar_t const* ACCENT_TEXT = L"AccentTextFillColorPrimaryBrush";
        inline constexpr wchar_t const* LAYER_BACKGROUND = L"LayerFillColorDefaultBrush";
        inline constexpr wchar_t const* DIVIDER = L"DividerStrokeColorDefaultBrush";
    }
}
