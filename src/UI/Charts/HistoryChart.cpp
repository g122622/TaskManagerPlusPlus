#include "UI/WinRTUI.h"

#include "UI/Charts/HistoryChart.h"

#include "UI/Diagnostics.h"
#include "UI/Theming/Controls.h"
#include "UI/Theming/Theme.h"

#include <algorithm>

using winrt::Microsoft::UI::Xaml::Controls::Canvas;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::Media::PenLineJoin;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
using winrt::Microsoft::UI::Xaml::Shapes::Polygon;
using winrt::Microsoft::UI::Xaml::Shapes::Polyline;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        /// Vertical padding so a value at the maximum does not touch the border.
        constexpr double PLOT_PADDING = 6.0;

        /// How translucent the area under the line is.
        constexpr double FILL_OPACITY = 0.18;

        /// Smallest plot height worth drawing. Below this the chart leaves the plot
        /// empty rather than drawing a squashed line.
        constexpr double MIN_PLOT_HEIGHT = 24.0;

        /// How many times a failing redraw reports itself before going quiet.
        /// Vertical divisions of the background grid. Ten rows makes each line worth ten percent,
        /// which is a readable increment at a glance.
        constexpr int GRID_ROWS = 10;

        /// Horizontal divisions. Fewer than the rows because the x axis is time rather than a
        /// scale, so the lines only need to break the curve into spans.
        constexpr int GRID_COLUMNS = 6;

        /// Opacity of a grid line. Deliberately faint: the grid is a reading aid, and a line strong
        /// enough to notice on its own competes with the curve it exists to help read.
        constexpr double GRID_LINE_OPACITY = 0.08;

        /// Stroke width of a grid line. A hairline rather than a full pixel, so it does not
        /// dominate a small cell.
        constexpr double GRID_LINE_THICKNESS = 0.5;

        [[nodiscard]] winrt::Windows::UI::Color _withAlpha(winrt::Windows::UI::Color color, double alpha)
        {
            color.A = static_cast<uint8_t>(std::clamp(alpha, 0.0, 1.0) * 255.0);
            return color;
        }
    }

    HistoryChart::HistoryChart(std::wstring_view title, winrt::Windows::UI::Color lineColor, double maximum)
        : m_color(lineColor), m_maximum(maximum > 0.0 ? maximum : 1.0)
    {
        m_root = Grid();

        // MakeAutoRow, not a default-constructed RowDefinition: GridLength defaults to
        // 1* (Star), so a default row takes an equal share of the height regardless of
        // what its content measures. Two defaults here split the cell in half and left the
        // plot canvas 16 pixels of its cell's 33 -- below the minimum needed to draw, so
        // every core chart silently rendered blank.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeStarRow());

        // Header: title on the left, current value on the right.
        m_header = Grid();
        // The title takes what it needs; the readout takes the rest, so a long value is
        // never clipped by a fixed column.
        m_header.ColumnDefinitions().Append(controls::MakeAutoColumn());
        m_header.ColumnDefinitions().Append(controls::MakeStarColumn());
        m_header.Margin(winrt::Microsoft::UI::Xaml::ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 6.0));

        m_title = controls::MakeText(title, 13.0, true);
        m_title.VerticalAlignment(VerticalAlignment::Center);

        // The readout is the largest text on the page, matching Task Manager's
        // emphasis on the current value over the history.
        m_currentValue = controls::MakeText(L"", 24.0);
        m_currentValue.HorizontalAlignment(HorizontalAlignment::Right);
        m_currentValue.VerticalAlignment(VerticalAlignment::Center);
        m_currentValue.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
        m_currentValue.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);

        Grid::SetColumn(m_title, 0);
        Grid::SetColumn(m_currentValue, 1);
        m_header.Children().Append(m_title);
        m_header.Children().Append(m_currentValue);

        Grid::SetRow(m_header, 0);
        m_root.Children().Append(m_header);

        // The plot area has no fixed height: the canvas stretches to its cell and the points are
        // computed from whatever size that turns out to be.
        //
        // The grid lines and the frame both live inside this control rather than being left to
        // each call site. Repeating them per caller is what let three chart implementations drift
        // to three different border colours, and the grid would have been forgotten entirely.
        m_canvas = Canvas();
        m_canvas.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_canvas.VerticalAlignment(VerticalAlignment::Stretch);

        m_fill = winrt::Microsoft::UI::Xaml::Shapes::Polygon();
        m_fill.Fill(SolidColorBrush(_withAlpha(m_color, FILL_OPACITY)));

        m_line = winrt::Microsoft::UI::Xaml::Shapes::Polyline();
        m_line.Stroke(SolidColorBrush(m_color));
        m_line.StrokeThickness(m_lineWidth);
        m_line.StrokeLineJoin(PenLineJoin::Round);

        // The optional second line, dashed. It is constructed here rather than lazily because every
        // redraw clears its points, and a lazily-created shape would leave that path dereferencing a
        // null pointer on every chart that does not use it.
        m_secondaryLine = winrt::Microsoft::UI::Xaml::Shapes::Polyline();
        m_secondaryLine.Stroke(SolidColorBrush(m_color));
        m_secondaryLine.StrokeThickness(m_lineWidth);
        m_secondaryLine.StrokeLineJoin(PenLineJoin::Round);

        // A dash pattern in stroke-width units, so it stays legible at any configured width; a fixed
        // pixel pattern would close up at a thin width and blur at a thick one.
        m_secondaryDashes = winrt::Microsoft::UI::Xaml::Media::DoubleCollection();
        m_secondaryDashes.Append(4.0);
        m_secondaryDashes.Append(3.0);
        m_secondaryLine.StrokeDashArray(m_secondaryDashes);

        // Hidden until a secondary series is supplied, so a chart that does not use one is unchanged.
        m_secondaryLine.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

        // The area fill goes in first so the line draws over it. Adding the line without these two
        // appends is what left the chart drawing its frame and grid but no curve: the shapes existed
        // and had their points set, but were never part of the visual tree.
        m_canvas.Children().Append(m_fill);
        m_canvas.Children().Append(m_line);
        m_canvas.Children().Append(m_secondaryLine);

        // The canvas is not a child of the frame directly: the frame holds a host so the grid layer
        // and the plot can coexist inside one outline.
        m_plotFrame = controls::MakeChartFrame(m_plotHost);
        m_plotHost.Children().Append(m_canvas);

        // The grid lines are drawn in their own canvas behind the plot. A separate layer means the
        // plot's redraw does not have to recreate them, and they stay put while the curve moves.
        m_gridCanvas = Canvas();
        m_gridCanvas.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_gridCanvas.VerticalAlignment(VerticalAlignment::Stretch);
        m_plotHost.Children().InsertAt(0, m_gridCanvas);

        // The grid depends on the size, so it is rebuilt whenever the plot is.
        m_gridCanvas.SizeChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                        winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&) { _drawGrid(); });

        Grid::SetRow(m_plotFrame, 1);
        m_root.Children().Append(m_plotFrame);

        // Redraw on resize, since the point coordinates are derived from the size.
        // Without this the curve would keep the width it had when the data arrived.
        m_canvas.SizeChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                    winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&) { _redraw(); });
    }

    void HistoryChart::SetSeries(ChartSeries const& series)
    {
        m_values = series.values;
        m_timeSpan = series.windowSamples;
        _redraw();
    }

    void HistoryChart::SetSecondarySeries(ChartSeries const& series)
    {
        m_secondaryValues = series.values;

        // An empty series removes the line rather than leaving the previous one on screen, which
        // would silently show stale data.
        if (m_secondaryLine != nullptr)
        {
            m_secondaryLine.Visibility(m_secondaryValues.empty() ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                                                 : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

        _redraw();
    }

    void HistoryChart::SetCurrentValueText(std::wstring_view text)
    {
        m_currentValue.Text(winrt::hstring{text});
    }

    void HistoryChart::SetLineColor(winrt::Windows::UI::Color color)
    {
        m_color = color;
        m_line.Stroke(SolidColorBrush(m_color));
        m_fill.Fill(SolidColorBrush(_withAlpha(m_color, FILL_OPACITY)));
    }

    void HistoryChart::SetHeaderVisible(bool visible)
    {
        m_header.Visibility(visible ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void HistoryChart::SetMaximum(double maximum)
    {
        // Zero or negative would divide by zero in the plotting arithmetic. One is the smallest
        // meaningful axis and leaves the plot empty, which is the honest picture of no data.
        m_maximum = (maximum > 0.0) ? maximum : 1.0;
        _redraw();
    }

    void HistoryChart::SetLineWidth(double width)
    {
        // Clamped rather than trusted: the value comes from the settings file, and a stroke of
        // zero would make the chart look empty while an enormous one would fill it solid.
        m_lineWidth = std::clamp(width, 0.5, 8.0);
        m_line.StrokeThickness(m_lineWidth);

        // The secondary line too. The two are one chart and are read against each other, so giving them
        // different weights would make the dashed series look like a different kind of measurement rather
        // than the second half of the same one. It was missed here, which left the dashed line at whatever
        // width it was constructed with while the solid one followed the setting.
        if (m_secondaryLine != nullptr)
        {
            m_secondaryLine.StrokeThickness(m_lineWidth);
        }
    }

    void HistoryChart::_drawGrid()
    {
        m_gridCanvas.Children().Clear();

        double const width = m_gridCanvas.ActualWidth();
        double const height = m_gridCanvas.ActualHeight();
        if (width <= 1.0 || height < MIN_PLOT_HEIGHT)
        {
            return;
        }

        // The grid is drawn in the muted text colour rather than a fixed grey, so it adapts to the
        // light and dark themes: a grey chosen for one theme is either invisible or too strong in
        // the other.
        auto const brush = controls::ThemedBrush(theme::SUBTLE_TEXT);
        brush.Opacity(GRID_LINE_OPACITY);

        auto addLine = [this, &brush](double x1, double y1, double x2, double y2) {
            winrt::Microsoft::UI::Xaml::Shapes::Line line;
            line.X1(x1);
            line.Y1(y1);
            line.X2(x2);
            line.Y2(y2);
            line.Stroke(brush);
            line.StrokeThickness(GRID_LINE_THICKNESS);
            m_gridCanvas.Children().Append(line);
        };

        // Horizontal lines first. They carry the value scale, so they are the ones the eye uses to
        // judge a curve's level.
        //
        // The division count scales with the plot: a ten by six grid in a 92 pixel cell is a dense
        // mesh rather than a guide, while the same count in a 600 pixel chart is a readable aid.
        // The original does the same, its per-core cells carrying noticeably fewer lines than its
        // large charts.
        int const rows = (height >= 220.0) ? GRID_ROWS : 4;
        int const columns = (width >= 420.0) ? GRID_COLUMNS : 3;

        // Placed through the same mapping the curve uses, so each line sits exactly where its value is.
        // Dividing the canvas into equal bands instead put them a few pixels out, most visibly at the
        // bottom, where the lowest line landed below the curve's own zero.
        //
        // The bottom line is not drawn: the frame's lower edge is the zero line.
        for (int i = 1; i < rows; ++i)
        {
            double const y = YForRatio(static_cast<double>(i) / static_cast<double>(rows), height, m_lineWidth);
            addLine(0.0, y, width, y);
        }

        // Vertical lines, which divide the time axis.
        for (int i = 1; i < columns; ++i)
        {
            double const x = (width / columns) * i;
            addLine(x, 0.0, x, height);
        }
    }

    double HistoryChart::YForRatio(double ratio, double height, double lineWidth)
    {
        // Half the stroke is reserved at the bottom because a stroke is centred on its path: a value of
        // zero drawn exactly on the bottom edge would have its lower half clipped away and would read as
        // thinner than the rest of the line. Reserving half a stroke keeps the whole line visible with its
        // centre still on zero.
        double const bottomInset = lineWidth / 2.0;
        double const drawableHeight = height - PLOT_PADDING - bottomInset;

        return PLOT_PADDING + (drawableHeight * (1.0 - ratio));
    }

    void HistoryChart::_redraw()
    {
        m_line.Points().Clear();
        m_fill.Points().Clear();
        m_secondaryLine.Points().Clear();

        double const width = m_canvas.ActualWidth();
        double const height = m_canvas.ActualHeight();

        // A single point cannot describe a line. Leaving the plot empty is honest;
        // drawing a flat line would claim a reading that does not exist.
        bool const canDraw = (m_values.size() >= 2) && width > 1.0 && height >= MIN_PLOT_HEIGHT;

        if (!canDraw)
        {
            return;
        }

        if (height - PLOT_PADDING - (m_lineWidth / 2.0) <= 0.0)
        {
            return;
        }

        // The plot area: zero on the bottom edge of the canvas, the maximum one padding below the top.
        //
        // The bottom used to carry the same padding as the top, which put a value of zero six pixels above
        // the frame's lower edge -- so a curve sitting at zero still had visible height, and every reading
        // was off by that padding.
        auto const yFor = [this, height](double ratio) { return YForRatio(ratio, height, m_lineWidth); };

        // The x axis is a fixed time window, right-anchored: the newest sample sits at the right
        // edge and older samples extend leftwards. A chart holding three of sixty samples
        // therefore draws a short line against the right edge with the rest of the window
        // empty, and the line grows leftwards as history accumulates until the window is full
        // and it scrolls.
        //
        // This is how the original behaves, and both alternatives are wrong. Fitting the
        // samples to the width drew three points across the whole chart, which reads as a
        // settled history that does not exist yet. Left-anchoring, which this did first, put
        // the newest sample at the left edge, so the chart appeared to be losing data as the
        // empty space moved rightwards.
        size_t const span = (m_timeSpan > 1) ? m_timeSpan : m_values.size();
        double const step = (span > 1) ? (width / static_cast<double>(span - 1)) : width;

        // The oldest sample's x offset: the line occupies the rightmost part of the window
        // when the data is shorter than the window, and the whole of it once full.
        size_t const samplesHeld = m_values.size();
        double const leadingGap = (span > samplesHeld) ? (width - (static_cast<double>(samplesHeld - 1) * step))
                                                       : 0.0;

        winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::Point> linePoints =
            m_line.Points();
        winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::Point> fillPoints =
            m_fill.Points();

        for (size_t i = 0; i < m_values.size(); ++i)
        {
            double const clamped = std::clamp(m_values[i], 0.0, m_maximum);
            double const ratio = clamped / m_maximum;

            // Y grows downward in a canvas, so the ratio is inverted.
            auto const x = static_cast<float>(leadingGap + (static_cast<double>(i) * step));
            auto const y = static_cast<float>(yFor(ratio));

            linePoints.Append(winrt::Windows::Foundation::Point{x, y});
            fillPoints.Append(winrt::Windows::Foundation::Point{x, y});
        }

        // Close the shape along the zero line. The fill spans only from the oldest sample to
        // the newest, so the part of the window that holds no readings yet stays visibly
        // empty rather than being shaded as though it held data.
        //
        // The baseline is the zero line rather than the canvas's bottom edge. Those were the same place
        // until the bottom padding was removed, so the fill used the edge; they are now the same place
        // again by construction, and using the mapping keeps them so if the padding ever changes.
        auto const firstX = static_cast<float>(leadingGap);
        auto const lastX = static_cast<float>(leadingGap + (static_cast<double>(samplesHeld - 1) * step));
        auto const baseline = static_cast<float>(yFor(0.0));
        fillPoints.Append(winrt::Windows::Foundation::Point{lastX, baseline});
        fillPoints.Append(winrt::Windows::Foundation::Point{firstX, baseline});

        // The second series shares the axis and the window, so it is plotted from the same step and
        // the same leading offset. It carries no fill: two shaded areas would obscure each other,
        // and the point of showing both is to compare the lines.
        if (!m_secondaryValues.empty() && m_secondaryValues.size() >= 2)
        {
            size_t const secondaryHeld = m_secondaryValues.size();
            double const secondaryGap = (span > secondaryHeld)
                                            ? (width - (static_cast<double>(secondaryHeld - 1) * step))
                                            : 0.0;

            auto secondaryPoints = m_secondaryLine.Points();
            for (size_t i = 0; i < secondaryHeld; ++i)
            {
                double const clamped = std::clamp(m_secondaryValues[i], 0.0, m_maximum);
                double const ratio = clamped / m_maximum;

                auto const x = static_cast<float>(secondaryGap + (static_cast<double>(i) * step));
                auto const y = static_cast<float>(yFor(ratio));
                secondaryPoints.Append(winrt::Windows::Foundation::Point{x, y});
            }
        }
    }
}
