#include "UI/WinRTUI.h"
#include <winrt/Windows.UI.Text.h>

#include "UI/HistoryChart.h"

#include "UI/Controls.h"
#include "UI/Theme.h"


#include <algorithm>
#include <cmath>

namespace tmpp::ui
{
    namespace
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using namespace winrt::Microsoft::UI::Xaml::Controls;
        using namespace winrt::Microsoft::UI::Xaml::Media;

        /// Chart drawing area inside the card, in effective pixels.
        constexpr double PLOT_HEIGHT = 140.0;

        /// Vertical padding so a 100% value does not touch the border.
        constexpr double PLOT_PADDING = 4.0;

        /// How translucent the area under the line is.
        constexpr double FILL_OPACITY = 0.18;

        /**
         * @brief Builds a translucent version of a colour for the area fill.
         */
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
        m_root.RowDefinitions().Append(RowDefinition{});
        m_root.RowDefinitions().Append(RowDefinition{});

        // Header: title on the left, current value on the right.
        Grid header = Grid();
        header.ColumnDefinitions().Append(ColumnDefinition{});
        header.ColumnDefinitions().Append(ColumnDefinition{});

        m_title = controls::MakeText(title, 13.0, true);

        m_currentValue = controls::MakeText(L"", 22.0);
        m_currentValue.HorizontalAlignment(HorizontalAlignment::Right);
        m_currentValue.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());

        Grid::SetColumn(m_title, 0);
        Grid::SetColumn(m_currentValue, 1);
        header.Children().Append(m_title);
        header.Children().Append(m_currentValue);

        Grid::SetRow(header, 0);
        m_root.Children().Append(header);

        // The plot area. A Canvas gives absolute positioning, which is what a line
        // chart needs; the sizes are updated on each redraw because the card can be
        // resized by the user.
        m_canvas = Canvas();
        m_canvas.Height(PLOT_HEIGHT);
        m_canvas.HorizontalAlignment(HorizontalAlignment::Stretch);

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

        // Redraw when the card is resized, so the curve always spans the full width
        // rather than being clipped or leaving a gap.
        m_canvas.SizeChanged([this](winrt::Windows::Foundation::IInspectable const&, SizeChangedEventArgs const&) { _redraw(); });
    }

    void HistoryChart::SetSeries(std::vector<double> const& values)
    {
        m_values = values;
        m_hasData = !m_values.empty();
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

    void HistoryChart::_redraw()
    {
        m_line.Points().Clear();
        m_fill.Points().Clear();

        double const width = m_canvas.ActualWidth();
        if (!m_hasData || width <= 1.0 || m_values.size() < 2)
        {
            // A single point cannot describe a line. Leaving the chart empty is
            // honest; drawing a flat line would claim a reading that does not exist.
            return;
        }

        double const plotWidth = width;
        double const drawableHeight = PLOT_HEIGHT - (PLOT_PADDING * 2.0);

        // Points are spaced across the full width regardless of the sample count so
        // the time axis always spans the whole history window.
        double const step = plotWidth / static_cast<double>(m_values.size() - 1);

        PointCollection linePoints;
        PointCollection fillPoints;

        for (size_t i = 0; i < m_values.size(); ++i)
        {
            double const clamped = std::clamp(m_values[i], 0.0, m_maximum);
            double const ratio = clamped / m_maximum;

            // Y grows downward in the canvas, so the ratio is inverted.
            double const x = static_cast<double>(i) * step;
            double const y = PLOT_PADDING + (drawableHeight * (1.0 - ratio));

            winrt::Windows::Foundation::Point const point{static_cast<float>(x), static_cast<float>(y)};
            linePoints.Append(point);
            fillPoints.Append(point);
        }

        // The fill closes the shape along the baseline, turning the line into an
        // area chart the way Task Manager draws it.
        fillPoints.Append(winrt::Windows::Foundation::Point{static_cast<float>(plotWidth), static_cast<float>(PLOT_HEIGHT)});
        fillPoints.Append(winrt::Windows::Foundation::Point{0.0f, static_cast<float>(PLOT_HEIGHT)});

        m_line.Points(linePoints);
        m_fill.Points(fillPoints);
    }
}
