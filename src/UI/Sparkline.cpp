#include "UI/WinRTUI.h"

#include "UI/Sparkline.h"

#include "UI/Theme.h"

#include <algorithm>
#include <cmath>

using winrt::Microsoft::UI::Xaml::Controls::Canvas;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
using winrt::Microsoft::UI::Xaml::Shapes::Polygon;
using winrt::Microsoft::UI::Xaml::Shapes::Polyline;

namespace tmpp::ui
{
    namespace
    {
        /// How translucent the area under the line is.
        constexpr double FILL_OPACITY = 0.22;

        /// Opacity applied to the line and fill when the row is not selected.
        constexpr double MUTED_OPACITY = 0.55;

        /// Stroke width. Thinner than the full chart, since the plot is much smaller.
        constexpr double STROKE_WIDTH = 1.5;

        [[nodiscard]] winrt::Windows::UI::Color _withAlpha(winrt::Windows::UI::Color color, double alpha)
        {
            color.A = static_cast<uint8_t>(std::clamp(alpha, 0.0, 1.0) * 255.0);
            return color;
        }
    }

    Sparkline::Sparkline(winrt::Windows::UI::Color color, double width, double height)
        : m_color(color), m_width(width), m_height(height)
    {
        m_root = winrt::Microsoft::UI::Xaml::Controls::Grid();
        m_root.Width(m_width);
        m_root.Height(m_height);

        m_canvas = Canvas();
        m_canvas.Width(m_width);
        m_canvas.Height(m_height);

        m_fill = winrt::Microsoft::UI::Xaml::Shapes::Polygon();
        m_fill.Fill(SolidColorBrush(_withAlpha(m_color, FILL_OPACITY)));

        m_line = winrt::Microsoft::UI::Xaml::Shapes::Polyline();
        m_line.Stroke(SolidColorBrush(m_color));
        m_line.StrokeThickness(STROKE_WIDTH);
        m_line.StrokeLineJoin(winrt::Microsoft::UI::Xaml::Media::PenLineJoin::Round);

        m_canvas.Children().Append(m_fill);
        m_canvas.Children().Append(m_line);
        m_root.Children().Append(m_canvas);
    }

    void Sparkline::SetSeries(std::vector<double> const& values, double maximum)
    {
        m_values = values;
        m_maximum = (maximum > 0.0) ? maximum : 1.0;
        _redraw();
    }

    void Sparkline::Clear()
    {
        m_values.clear();
        m_line.Points().Clear();
        m_fill.Points().Clear();
    }

    void Sparkline::SetColors(winrt::Windows::UI::Color color, bool muted)
    {
        m_color = color;

        double const lineAlpha = muted ? MUTED_OPACITY : 1.0;
        m_line.Stroke(SolidColorBrush(_withAlpha(m_color, lineAlpha)));
        m_fill.Fill(SolidColorBrush(_withAlpha(m_color, muted ? FILL_OPACITY * MUTED_OPACITY : FILL_OPACITY)));
    }

    void Sparkline::_redraw()
    {
        m_line.Points().Clear();
        m_fill.Points().Clear();

        // A single point cannot describe a line. Leaving the plot empty is honest;
        // drawing a flat line would claim a reading that does not exist.
        if (m_values.size() < 2 || m_width <= 1.0 || m_height <= 1.0)
        {
            return;
        }

        double const step = m_width / static_cast<double>(m_values.size() - 1);

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
            auto const y = static_cast<float>(m_height * (1.0 - ratio));

            linePoints.Append(winrt::Windows::Foundation::Point{x, y});
            fillPoints.Append(winrt::Windows::Foundation::Point{x, y});
        }

        // Close the shape along the baseline to make it an area chart.
        fillPoints.Append(winrt::Windows::Foundation::Point{static_cast<float>(m_width), static_cast<float>(m_height)});
        fillPoints.Append(winrt::Windows::Foundation::Point{0.0f, static_cast<float>(m_height)});
    }
}
