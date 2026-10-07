#include "UI/WinRTUI.h"

#include "UI/Charts/MemoryCompositionBar.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Theme.h"

#include <algorithm>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::Canvas;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::Media::RectangleGeometry;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
using winrt::Microsoft::UI::Xaml::Shapes::Line;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::UIElement;
using winrt::Windows::Foundation::Rect;

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

        /// Horizontal period of the compression hatch.
        ///
        /// The lines lean at 45 degrees, so their perpendicular spacing is this divided by the
        /// square root of two -- about five pixels, which is the density the original's hatch reads
        /// at: a texture you notice without counting its lines.
        constexpr double HATCH_STEP = 7.0;

        /// Stroke width of one hatch line. Kept thin deliberately: a heavier line turns the hatch
        /// into a second colour laid over the segment rather than a texture on it.
        constexpr double HATCH_THICKNESS = 1.0;

        /// Opacity of the hatch lines. White at full strength glares against the segment colour --
        /// most of all in the light theme, where the strip sits on a bright page -- and the hatch is
        /// meant to be read as a texture rather than as a brighter block.
        constexpr double HATCH_OPACITY = 0.55;

        /**
         * @brief Builds the content of a segment whose leading part is hatched.
         *
         * Two star columns proportional to the hatched and plain lengths, for the same reason the
         * strip itself is built from stars: the division follows the window without any pixel
         * arithmetic, and it stays exact when the segment is only a few pixels wide.
         */
        [[nodiscard]] UIElement _hatchedContent(MemoryCompositionBar::Segment const& segment)
        {
            Canvas const hatched = MakeHatchedSurface(segment.color);

            // A segment hatched along its whole length has no plain part to divide against, so the
            // surface is the content and the division is skipped. Splitting a star column in two at
            // a zero weight would leave a seam where nothing is drawn.
            if (segment.hatchedBytes >= segment.bytes)
            {
                return hatched;
            }

            Grid inner;

            ColumnDefinition hatchedColumn;
            hatchedColumn.Width(GridLengthHelper::FromValueAndType(static_cast<double>(segment.hatchedBytes),
                                                                   GridUnitType::Star));
            inner.ColumnDefinitions().Append(hatchedColumn);

            ColumnDefinition plainColumn;
            plainColumn.Width(GridLengthHelper::FromValueAndType(static_cast<double>(segment.bytes - segment.hatchedBytes),
                                                                 GridUnitType::Star));
            inner.ColumnDefinitions().Append(plainColumn);

            Grid::SetColumn(hatched, 0);
            inner.Children().Append(hatched);

            // Nothing is placed in the second column: the piece's own background is the plain part,
            // and an element there would only repaint the colour already showing through.
            return inner;
        }
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
                Segment visible = segment;

                // The hatched lead is a part of its own segment, so it cannot be longer than the
                // segment is. Clamped here rather than trusted from the caller, because the two
                // figures come from different sources -- a page list and a process's residency --
                // and a hatch wider than its segment would paint over the neighbour to its right.
                visible.hatchedBytes = (std::min)(visible.hatchedBytes, visible.bytes);
                m_visible.push_back(visible);
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

            if (segment.hatchedBytes > 0)
            {
                // Drawn inside the piece, whose background is already the segment's colour, so the
                // hatched lead and the plain remainder are visibly one segment.
                piece.Child(_hatchedContent(segment));
            }

            // The label is set for accessibility even though it is not drawn: a screen reader
            // announcing "27 percent" without saying of what is worse than no label at all. A
            // hatched part carries no label of its own here; the figure it stands for is named in
            // the legend, which is real text and is read in its own right.
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

    Canvas MakeHatchedSurface(winrt::Windows::UI::Color color)
    {
        Canvas surface;
        surface.Background(SolidColorBrush(color));
        // Decorative only. Nothing about the hatch is interactive, and taking it out of hit testing
        // leaves the pointer to whatever lies behind the strip.
        surface.IsHitTestVisible(false);

        SolidColorBrush stroke{winrt::Windows::UI::Colors::White()};
        stroke.Opacity(HATCH_OPACITY);

        // The surface is sized by its layout slot, which is not known until it has been measured, so
        // the stripes come from the size the layout reports. A weak reference rather than a captured
        // one: the handler is owned by the surface, and holding it strongly would make the two keep
        // each other alive for every sample that rebuilt the strip.
        winrt::weak_ref<Canvas> const weakSurface{surface};
        surface.SizeChanged([weakSurface, stroke](winrt::Windows::Foundation::IInspectable const&,
                                                  winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args) {
            Canvas const canvas = weakSurface.get();
            if (canvas == nullptr)
            {
                return;
            }

            canvas.Children().Clear();

            double const width = args.NewSize().Width;
            double const height = args.NewSize().Height;
            if (width <= 0.0 || height <= 0.0)
            {
                return;
            }

            // A line leaning at 45 degrees is as wide as the surface is tall, so it reaches that far
            // past where it starts. Clipping to the surface is what keeps the last line from being
            // painted over the plain part of the segment beside it, which would make the hatched
            // length read as longer than it is.
            RectangleGeometry clip;
            clip.Rect(Rect{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height)});
            canvas.Clip(clip);

            // The run starts one line-length before the left edge, because a line only enters the
            // surface after rising that far from where it starts: without it the bottom-left corner
            // would be the one bare patch in the hatch.
            for (double x = -height; x < width; x += HATCH_STEP)
            {
                Line line;
                line.X1(x);
                line.Y1(height);
                line.X2(x + height);
                line.Y2(0.0);
                line.Stroke(stroke);
                line.StrokeThickness(HATCH_THICKNESS);
                canvas.Children().Append(line);
            }
        });

        return surface;
    }
}
