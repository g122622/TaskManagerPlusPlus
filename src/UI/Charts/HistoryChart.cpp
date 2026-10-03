#include "UI/WinRTUI.h"

#include "UI/Charts/HistoryChart.h"

#include "UI/Theming/Controls.h"
#include "UI/Diagnostics.h"
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
        constexpr uint32_t MAX_REPORTS = 6;

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

        // The plot area has no fixed height: the canvas stretches to its cell and the
        // points are computed from whatever size that turns out to be.
        m_canvas = Canvas();
        m_canvas.HorizontalAlignment(HorizontalAlignment::Stretch);
        m_canvas.VerticalAlignment(VerticalAlignment::Stretch);

        m_fill = winrt::Microsoft::UI::Xaml::Shapes::Polygon();
        m_fill.Fill(SolidColorBrush(_withAlpha(m_color, FILL_OPACITY)));

        m_line = winrt::Microsoft::UI::Xaml::Shapes::Polyline();
        m_line.Stroke(SolidColorBrush(m_color));
        m_line.StrokeThickness(2.0);
        m_line.StrokeLineJoin(PenLineJoin::Round);

        m_canvas.Children().Append(m_fill);
        m_canvas.Children().Append(m_line);

        Grid::SetRow(m_canvas, 1);
        m_root.Children().Append(m_canvas);

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

    void HistoryChart::_redraw()
    {
        m_line.Points().Clear();
        m_fill.Points().Clear();

        double const width = m_canvas.ActualWidth();
        double const height = m_canvas.ActualHeight();

        // A single point cannot describe a line. Leaving the plot empty is honest;
        // drawing a flat line would claim a reading that does not exist.
        bool const canDraw = (m_values.size() >= 2) && width > 1.0 && height >= MIN_PLOT_HEIGHT;

        if (!canDraw)
        {
            return;
        }

        double const drawableHeight = height - (PLOT_PADDING * 2.0);
        if (drawableHeight <= 0.0)
        {
            return;
        }

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
            auto const y = static_cast<float>(PLOT_PADDING + (drawableHeight * (1.0 - ratio)));

            linePoints.Append(winrt::Windows::Foundation::Point{x, y});
            fillPoints.Append(winrt::Windows::Foundation::Point{x, y});
        }

        // Close the shape along the baseline. The fill spans only from the oldest sample to
        // the newest, so the part of the window that holds no readings yet stays visibly
        // empty rather than being shaded as though it held data.
        auto const firstX = static_cast<float>(leadingGap);
        auto const lastX = static_cast<float>(leadingGap + (static_cast<double>(samplesHeld - 1) * step));
        fillPoints.Append(winrt::Windows::Foundation::Point{lastX, static_cast<float>(height)});
        fillPoints.Append(winrt::Windows::Foundation::Point{firstX, static_cast<float>(height)});
    }
}
