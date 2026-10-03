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

        /// Left inset for a page that sits beside the navigation rail. Smaller than PAGE_MARGIN
        /// because the rail already provides the separation from the window edge.
        inline constexpr double CONTENT_LEFT_INSET = 8.0;

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

        /// Width of the performance page's own hardware sidebar. Wider than the
        /// navigation rail because each row carries a name, a qualifier and a chart.
        inline constexpr double PERFORMANCE_SIDEBAR_WIDTH = 300.0;

        /// Height of a performance sidebar row.
        inline constexpr double SIDEBAR_ROW_HEIGHT = 58.0;

        /// Height reserved for the status bar.
        inline constexpr double STATUS_BAR_HEIGHT = 28.0;

        /// Thickness of the outline drawn around every chart.
        ///
        /// One pixel, matching the original. Kept here rather than at each call site so the
        /// charts cannot disagree about it.
        inline constexpr double CHART_BORDER_THICKNESS = 1.0;

        /// Corner radius of a chart outline.
        inline constexpr double CHART_CORNER_RADIUS = 2.0;

        /// Opacity of a chart outline. Low because a per-core grid puts dozens of frames on screen,
        /// and at full strength they form a mesh that competes with the curves.
        inline constexpr double CHART_BORDER_OPACITY = 0.45;

        /// Hit area of a sidebar resize handle. Wide enough to grab without being visible as a gap.
        inline constexpr double SPLITTER_WIDTH = 6.0;
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

        /**
         * @brief The brush used to outline a chart.
         *
         * A distinct key from CARD_BORDER so that changing how charts are outlined cannot shift the
         * borders on cards, which are a different visual element.
         *
         * Deliberately the subtle stroke rather than the strong one. With a per-core grid there are
         * dozens of frames on screen at once, and the strong stroke turns them into a mesh of boxes
         * that competes with the curves for attention. The frame only needs to say where the plot
         * ends.
         */
        inline constexpr wchar_t const* CHART_BORDER = L"ControlStrokeColorDefaultBrush";
    }
}
