#include "UI/WinRTUI.h"

#include "UI/Charts/MemoryCompositionBar.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Theme.h"

#include <algorithm>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;

namespace tmpp::ui
{
    namespace
    {
        /// Height of the strip, matching the original's proportions against its chart.
        constexpr double BAR_HEIGHT = 26.0;

        /// Hairline between segments. A small gap reads as a division without needing borders on
        /// every piece, which would double up where two segments meet.
        constexpr double SEGMENT_GAP = 1.0;

        /// Corner radius of the strip. Smaller than a chart's, because a short bar with the chart
        /// radius reads as a pill rather than as a bar.
        constexpr double STRIP_CORNER_RADIUS = 3.0;
    }

    MemoryCompositionBar::MemoryCompositionBar()
    {
        m_root = Grid();

        // A frame around the whole strip, as the original draws. The segments inside are inset by
        // it, which keeps the outline crisp rather than being overdrawn by the fills.
        // The same factory as every chart, with a small radius: the strip is short, and the chart
        // radius on a 26 pixel bar would round its ends into a pill.
        m_segments = Grid();
        m_segments.ColumnSpacing(SEGMENT_GAP);

        // The segments fill the frame edge to edge, so their outer corners would square off the
        // frame's radius. A rounded Border around them keeps the ends round without any geometry
        // arithmetic.
        m_clip = winrt::Microsoft::UI::Xaml::Controls::Border();
        m_clip.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(STRIP_CORNER_RADIUS));
        m_clip.Child(m_segments);

        // Deliberately no outline. This is a filled bar rather than a plot, and an outline around it
        // reads as a chart frame -- it competes with the chart directly above it, which is the only
        // thing on the page that should be framed. The rounded clip alone gives the shape.
        m_clip.Height(BAR_HEIGHT);
        m_root.Children().Append(m_clip);
    }

    void MemoryCompositionBar::SetSegments(std::vector<Segment> const& segments)
    {
        // Segments with nothing in them are dropped rather than given a zero-width column: a
        // zero-width column still contributes its gap, which would leave a visible notch in the
        // strip for a category that does not exist.
        m_visible.clear();
        for (Segment const& segment : segments)
        {
            if (segment.bytes > 0)
            {
                m_visible.push_back(segment);
            }
        }

        _rebuild();
    }

    void MemoryCompositionBar::Clear()
    {
        m_visible.clear();
        _rebuild();
    }

    void MemoryCompositionBar::_rebuild()
    {
        m_segments.Children().Clear();
        m_segments.ColumnDefinitions().Clear();

        for (Segment const& segment : m_visible)
        {
            // Star columns proportional to the byte counts are what make the areas correct. The
            // Grid distributes the width by weight, so no pixel arithmetic is needed and the
            // strip stays exact at any window size.
            ColumnDefinition column;
            column.Width(GridLengthHelper::FromValueAndType(static_cast<double>(segment.bytes), GridUnitType::Star));
            m_segments.ColumnDefinitions().Append(column);

            Border piece;
            piece.Background(SolidColorBrush(segment.color));

            // The label is set for accessibility even though it is not drawn: a screen reader
            // announcing "27 percent" without saying of what is worse than no label at all.
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
                piece, winrt::hstring{segment.label});

            Grid::SetColumn(piece, static_cast<int32_t>(m_segments.Children().Size()));
            m_segments.Children().Append(piece);
        }

        // With nothing to show, the strip would be an empty rounded rectangle. Collapsing it
        // entirely is clearer: an empty strip looks like a measurement of nothing.
        m_clip.Visibility(m_visible.empty() ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                            : winrt::Microsoft::UI::Xaml::Visibility::Visible);


    }
}
