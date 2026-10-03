#include "UI/WinRTUI.h"

#include "UI/Views/GpuPage.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        constexpr double HEADING_FONT_SIZE = 22.0;
        constexpr double CHART_MIN_HEIGHT = 220.0;

        /// The default GPU line colour, matching the sidebar's GPU row.
        constexpr winrt::Windows::UI::Color DEFAULT_GPU_COLOR{0xFF, 0xFF, 0x8A, 0xA8};
    }

    GpuPage::GpuPage(core::SamplingCoordinator& coordinator) : m_coordinator(coordinator)
    {
        _buildLayout();
    }

    void GpuPage::_buildLayout()
    {
        m_root = Grid();

        // Rows: heading (Auto), caption (Auto), chart (star), details (Auto). A default-constructed
        // RowDefinition is 1* (Star), so the helpers are what stop the heading and details taking an
        // equal share of the height.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeStarRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());

        // --- Heading -----------------------------------------------------------
        Grid headingRow = Grid();
        headingRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        headingRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

        m_heading = controls::MakeHeading(L"GPU", HEADING_FONT_SIZE);
        Grid::SetColumn(m_heading, 0);
        headingRow.Children().Append(m_heading);

        // The adapter's memory, at the right-hand end of the heading, as the original shows it.
        m_memoryCaption = controls::MakeText(L"", 13.0, true);
        m_memoryCaption.VerticalAlignment(VerticalAlignment::Bottom);
        m_memoryCaption.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_memoryCaption, 1);
        headingRow.Children().Append(m_memoryCaption);

        Grid::SetRow(headingRow, 0);
        m_root.Children().Append(headingRow);

        // --- Caption -----------------------------------------------------------
        Grid captionRow = Grid();
        captionRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        captionRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
        captionRow.Margin(ThicknessHelper::FromLengths(0.0, 10.0, 0.0, 4.0));

        m_caption = controls::MakeText(L"", 12.0, true);
        m_caption.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(m_caption, 0);
        captionRow.Children().Append(m_caption);

        TextBlock maximumLabel = controls::MakeText(L"100%", 12.0, true);
        maximumLabel.HorizontalAlignment(HorizontalAlignment::Right);
        Grid::SetColumn(maximumLabel, 1);
        captionRow.Children().Append(maximumLabel);

        Grid::SetRow(captionRow, 1);
        m_root.Children().Append(captionRow);

        // --- Utilisation chart -------------------------------------------------
        //
        // Utilisation is a share of the busiest engine, so the axis is a real proportion and 100
        // percent means that engine is saturated.
        m_chart = std::make_unique<HistoryChart>(L"", DEFAULT_GPU_COLOR, 100.0);
        m_chart->SetHeaderVisible(false);
        m_chart->Root().MinHeight(CHART_MIN_HEIGHT);

        Grid::SetRow(m_chart->Root(), 2);
        m_root.Children().Append(m_chart->Root());

        // --- Details -----------------------------------------------------------
        Grid details = Grid();
        details.Margin(ThicknessHelper::FromLengths(0.0, 16.0, 0.0, 0.0));

        for (int i = 0; i < 3; ++i)
        {
            details.ColumnDefinitions().Append(controls::MakeStarColumn());
        }

        StackPanel column1 = controls::MakeStack(4.0);
        StackPanel column2 = controls::MakeStack(4.0);
        StackPanel column3 = controls::MakeStack(4.0);

        winrt::Microsoft::UI::Xaml::Thickness const padding =
            ThicknessHelper::FromLengths(0.0, 0.0, 24.0, 0.0);
        column1.Padding(padding);
        column2.Padding(padding);
        column3.Padding(padding);

        // Column 1: the headline figures.
        m_column1.push_back(_addDetail(column1, L"Utilisation"));
        m_column1.push_back(_addDetail(column1, L"Dedicated memory"));
        m_column1.push_back(_addDetail(column1, L"Shared memory"));
        m_column1.push_back(_addDetail(column1, L"Memory total"));

        // Column 2: the adapter's identity.
        m_column2.push_back(_addDetail(column2, L"Adapter"));
        m_column2.push_back(_addDetail(column2, L"Driver"));
        m_column2.push_back(_addDetail(column2, L"DirectX"));
        m_column2.push_back(_addDetail(column2, L"Hardware scheduled"));

        // Column 3: the engines the original breaks utilisation into.
        m_column3.push_back(_addDetail(column3, L"3D"));
        m_column3.push_back(_addDetail(column3, L"Copy"));
        m_column3.push_back(_addDetail(column3, L"Video decode"));
        m_column3.push_back(_addDetail(column3, L"Video encode"));

        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 3);
        m_root.Children().Append(details);
    }

    GpuPage::DetailRow GpuPage::_addDetail(StackPanel const& column, wchar_t const* label)
    {
        Grid row = Grid();
        row.ColumnDefinitions().Append(controls::MakeAutoColumn());
        row.ColumnDefinitions().Append(controls::MakeStarColumn());

        DetailRow detail;
        detail.label = controls::MakeText(label, 13.0, true);
        detail.label.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);

        detail.value = controls::MakeText(winrt::to_hstring(UnavailableValue()), 13.0);
        detail.value.HorizontalAlignment(HorizontalAlignment::Right);
        detail.value.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        detail.value.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

        Grid::SetColumn(detail.label, 0);
        Grid::SetColumn(detail.value, 1);
        row.Children().Append(detail.label);
        row.Children().Append(detail.value);

        column.Children().Append(row);
        return detail;
    }

    void GpuPage::SetAccentColor(winrt::Windows::UI::Color color)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineColor(color);
        }
    }

    void GpuPage::SetLineWidth(double width)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineWidth(width);
        }
    }

    void GpuPage::Refresh()
    {
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        if (m_heading != nullptr && system.gpu.available && !system.gpu.adapterName.empty())
        {
            m_heading.Text(winrt::to_hstring(system.gpu.adapterName));
        }

        // Memory is stated as used against total, because a figure on its own says nothing about
        // whether it is close to the limit.
        if (m_memoryCaption != nullptr)
        {
            if (system.gpu.available && system.gpu.dedicatedTotalBytes > 0)
            {
                m_memoryCaption.Text(winrt::to_hstring(
                    FormatBytes(system.gpu.dedicatedUsedBytes) + " / " +
                    FormatBytes(system.gpu.dedicatedTotalBytes)));
            }
            else
            {
                m_memoryCaption.Text(winrt::to_hstring(UnavailableValue()));
            }
        }

        m_caption.Text(winrt::to_hstring("Utilisation  " +
                                         (system.gpu.available ? FormatPercent(system.gpu.utilizationPercent)
                                                               : std::string{UnavailableValue()}) +
                                         "  \xE2\x80\xA2  over " + std::to_string(m_coordinator.HistorySeconds()) +
                                         " s"));

        // The window rides with the data, so the chart anchors its samples to the right edge rather
        // than stretching a handful of them across the full width.
        ChartSeries series;
        series.values = history.gpuUtilization;
        series.windowSamples = history.windowSamples;
        m_chart->SetSeries(series);

        _updateDetails(system);
    }

    void GpuPage::_updateDetails(domain::SystemView const& system)
    {
        auto assign = [](std::vector<DetailRow> const& rows, size_t index, std::string const& text) {
            if (index < rows.size() && rows[index].value != nullptr)
            {
                rows[index].value.Text(winrt::to_hstring(text));
            }
        };

        if (!system.gpu.available)
        {
            // A machine without GPU counters, or the frame before the first reading. A dash is
            // honest; a zero would read as a measurement that was taken.
            for (size_t i = 0; i < m_column1.size(); ++i)
            {
                assign(m_column1, i, UnavailableValue());
                assign(m_column2, i, UnavailableValue());
                assign(m_column3, i, UnavailableValue());
            }
            return;
        }

        assign(m_column1, 0, FormatPercent(system.gpu.utilizationPercent));
        assign(m_column1, 1, system.gpu.dedicatedUsedBytes > 0 ? FormatBytes(system.gpu.dedicatedUsedBytes)
                                                               : UnavailableValue());
        // Shared memory is reported even when it is small, since a discrete adapter using system
        // memory is worth seeing.
        assign(m_column1, 2, system.gpu.sharedUsedBytes > 0 ? FormatBytes(system.gpu.sharedUsedBytes)
                                                            : std::string{"0 B"});
        assign(m_column1, 3, system.gpu.dedicatedTotalBytes > 0 ? FormatBytes(system.gpu.dedicatedTotalBytes)
                                                                : UnavailableValue());

        assign(m_column2, 0, system.gpu.adapterName.empty() ? UnavailableValue() : system.gpu.adapterName);

        assign(m_column2, 1, system.gpu.driverVersion.empty() ? UnavailableValue() : system.gpu.driverVersion);

        // The DirectX feature level and hardware scheduling would need a D3D device and a scheduler
        // query respectively, neither of which this probe makes; a dash is honest where a zero would
        // read as "not supported".
        assign(m_column2, 2, UnavailableValue());
        assign(m_column2, 3, UnavailableValue());

        // Per-engine utilisation, which is what the original breaks its figure into.
        assign(m_column3, 0, FormatPercent(system.gpu.engine3dPercent));
        assign(m_column3, 1, FormatPercent(system.gpu.engineCopyPercent));
        assign(m_column3, 2, FormatPercent(system.gpu.engineVideoDecodePercent));
        assign(m_column3, 3, FormatPercent(system.gpu.engineVideoEncodePercent));
    }
}
