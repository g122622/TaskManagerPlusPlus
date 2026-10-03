#include "UI/WinRTUI.h"

#include "UI/HistoryChart.h"

#include "UI/Controls.h"
#include "UI/Theme.h"

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

        // Row 0 is the header, sized to its content. Row 1 is the plot and takes all
        // remaining space, which is what makes the chart fill its cell instead of
        // being a fixed-height band inside it.
        m_root.RowDefinitions().Append(RowDefinition{});
        RowDefinition plotRow;
        plotRow.Height(GridLengthHelper::FromValueAndType(1.0, winrt::Microsoft::UI::Xaml::GridUnitType::Star));
        m_root.RowDefinitions().Append(plotRow);

        // Header: title on the left, current value on the right.
        m_header = Grid();
        m_header.ColumnDefinitions().Append(ColumnDefinition{});
        m_header.ColumnDefinitions().Append(ColumnDefinition{});
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

    void HistoryChart::SetSeries(std::vector<double> const& values)
    {
        m_values = values;
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
        if (m_values.size() < 2 || width <= 1.0 || height < MIN_PLOT_HEIGHT)
        {
            return;
        }

        double const drawableHeight = height - (PLOT_PADDING * 2.0);
        if (drawableHeight <= 0.0)
        {
            return;
        }

        // Points span the full width regardless of the sample count, so the time axis
        // always covers the whole history window.
        double const step = width / static_cast<double>(m_values.size() - 1);

        winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::Point> linePoints =
            m_line.Points();
        winrt::Windows::Foundation::Collections::IVector<winrt::Windows::Foundation::Point> fillPoints =
            m_fill.Points();

        for (size_t i = 0; i < m_values.size(); ++i)
        {
            double const clamped = std::clamp(m_values[i], 0.0, m_maximum);
            double const ratio = clamped / m_maximum;

            // Y grows downward in a canvas, so the ratio is inverted.
            auto const x = static_cast<float>(static_cast<double>(i) * step);
            auto const y = static_cast<float>(PLOT_PADDING + (drawableHeight * (1.0 - ratio)));

            linePoints.Append(winrt::Windows::Foundation::Point{x, y});
            fillPoints.Append(winrt::Windows::Foundation::Point{x, y});
        }

        // Close the shape along the baseline, turning the line into an area chart the
        // way Task Manager draws it.
        fillPoints.Append(winrt::Windows::Foundation::Point{static_cast<float>(width), static_cast<float>(height)});
        fillPoints.Append(winrt::Windows::Foundation::Point{0.0f, static_cast<float>(height)});
    }
}
