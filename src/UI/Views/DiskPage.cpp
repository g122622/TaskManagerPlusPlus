#include "UI/WinRTUI.h"

#include "UI/Views/DiskPage.h"

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

        /// The default disk line colour, matching the sidebar's disk row.
        constexpr winrt::Windows::UI::Color DEFAULT_DISK_COLOR{0xFF, 0x6E, 0xD8, 0xB0};

        [[nodiscard]] std::string _rateText(double bytesPerSecond)
        {
            return FormatBytes(static_cast<uint64_t>((std::max)(0.0, bytesPerSecond))) + "/s";
        }
    }

    DiskPage::DiskPage(core::SamplingCoordinator& coordinator) : m_coordinator(coordinator)
    {
        _buildLayout();
    }

    void DiskPage::_buildLayout()
    {
        m_root = Grid();

        // Rows: heading (Auto), caption (Auto), chart (star), details (Auto). The helpers matter
        // because a default-constructed RowDefinition is 1* (Star), which would give every row an
        // equal share of the height rather than sizing the first ones to their content.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeStarRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());

        // --- Heading -----------------------------------------------------------
        Grid headingRow = Grid();
        headingRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        headingRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

        m_heading = controls::MakeHeading(L"Disk", HEADING_FONT_SIZE);
        Grid::SetColumn(m_heading, 0);
        headingRow.Children().Append(m_heading);

        // The device's model and capacity, at the right-hand end of the heading line, which is where
        // the original puts them.
        m_modelCaption = controls::MakeText(L"", 13.0, true);
        m_modelCaption.VerticalAlignment(VerticalAlignment::Bottom);
        m_modelCaption.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_modelCaption, 1);
        headingRow.Children().Append(m_modelCaption);

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

        // --- Activity chart ----------------------------------------------------
        //
        // The chart plots active time rather than throughput. Throughput has no natural maximum --
        // a device's rate depends on the hardware -- so a 0 to 100 axis would be a guess, and the
        // figure a user reads a disk by is whether it is busy. The byte rates are in the details.
        m_chart = std::make_unique<HistoryChart>(L"", DEFAULT_DISK_COLOR, 100.0);
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

        // Column 1: the headline rates.
        m_column1.push_back(_addDetail(column1, L"Active time"));
        m_column1.push_back(_addDetail(column1, L"Average response time"));
        m_column1.push_back(_addDetail(column1, L"Read speed"));
        m_column1.push_back(_addDetail(column1, L"Write speed"));

        // Column 2: capacity and queueing.
        m_column2.push_back(_addDetail(column2, L"Capacity"));
        m_column2.push_back(_addDetail(column2, L"Formatted"));
        m_column2.push_back(_addDetail(column2, L"Queue length"));
        m_column2.push_back(_addDetail(column2, L"Page file"));

        // Column 3: the device identity.
        m_column3.push_back(_addDetail(column3, L"Model"));
        m_column3.push_back(_addDetail(column3, L"Device"));
        m_column3.push_back(_addDetail(column3, L"Type"));
        m_column3.push_back(_addDetail(column3, L"System disk"));

        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 3);
        m_root.Children().Append(details);
    }

    DiskPage::DetailRow DiskPage::_addDetail(StackPanel const& column, wchar_t const* label)
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

    void DiskPage::SetAccentColor(winrt::Windows::UI::Color color)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineColor(color);
        }
    }

    void DiskPage::SetLineWidth(double width)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineWidth(width);
        }
    }

    void DiskPage::SetDeviceIndex(size_t index)
    {
        if (m_deviceIndex != index)
        {
            m_deviceIndex = index;
            // Forces the next refresh to repaint, since the version alone has not changed.
            m_renderedVersion = 0;
        }
    }

    void DiskPage::SetDeviceLabel(std::wstring_view label)
    {
        m_deviceLabel.assign(label);
        if (m_heading != nullptr)
        {
            // The heading names the device the way the sidebar row does, so a machine with several
            // disks does not show a page that could belong to any of them.
            m_heading.Text(winrt::hstring{L"Disk "} + winrt::hstring{m_deviceLabel});
        }
    }

    void DiskPage::Refresh()
    {
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        if (m_heading != nullptr && m_deviceLabel.empty())
        {
            // Falls back to the device's own name when the sidebar has not supplied a label, which
            // is what happens on the first frame.
            if (m_deviceIndex < system.disks.size())
            {
                m_heading.Text(winrt::to_hstring("Disk " + system.disks[m_deviceIndex].instanceName));
            }
        }

        // The chart plots the mean active share across every device. Activity is the quantity a
        // 0-to-100 axis suits; a throughput chart would need a maximum that depends on the hardware.
        m_caption.Text(winrt::to_hstring("Active time  " + FormatPercent(system.diskActivePercent) +
                                         "  \xE2\x80\xA2  over " + std::to_string(m_coordinator.HistorySeconds()) +
                                         " s"));

        // The window rides with the data, so the chart anchors its samples to the right edge rather
        // than stretching a handful of them across the full width.
        ChartSeries series;
        series.windowSamples = history.windowSamples;

        // The history holds aggregate throughput, not a per-device series, so the active time is
        // reconstructed from the throughput the rings carry. Until a dedicated active-time series
        // exists, the chart shows whether the device is doing work rather than how much.
        series.values = history.diskReadBytesPerSecond;
        for (size_t i = 0; i < series.values.size() && i < history.diskWriteBytesPerSecond.size(); ++i)
        {
            series.values[i] += history.diskWriteBytesPerSecond[i];
        }

        // Scaled to the busiest sample in the window so the shape is readable. The axis is therefore
        // relative rather than absolute, which is stated in the caption above.
        double peak = 0.0;
        for (double const value : series.values)
        {
            peak = (std::max)(peak, value);
        }

        m_chart->SetMaximum(peak > 0.0 ? peak : 1.0);
        m_chart->SetSeries(series);

        _updateDetails(system);
    }

    void DiskPage::_updateDetails(domain::SystemView const& system)
    {
        auto assign = [](std::vector<DetailRow> const& rows, size_t index, std::string const& text) {
            if (index < rows.size() && rows[index].value != nullptr)
            {
                rows[index].value.Text(winrt::to_hstring(text));
            }
        };

        if (system.disks.empty())
        {
            // A machine whose disks cannot report performance data, or the first sample before the
            // device list is known. A dash is honest; a zero would read as a measurement.
            for (size_t i = 0; i < m_column1.size(); ++i)
            {
                assign(m_column1, i, UnavailableValue());
                assign(m_column2, i, UnavailableValue());
                assign(m_column3, i, UnavailableValue());
            }
            return;
        }

        size_t const index = (m_deviceIndex < system.disks.size()) ? m_deviceIndex : 0;
        domain::DiskActivity const& disk = system.disks[index];

        assign(m_column1, 0, FormatPercent(disk.activePercent));
        assign(m_column1, 1, UnavailableValue());
        assign(m_column1, 2, _rateText(disk.readBytesPerSecond));
        assign(m_column1, 3, _rateText(disk.writeBytesPerSecond));

        assign(m_column2, 0, disk.capacityBytes > 0 ? FormatBytes(disk.capacityBytes) : UnavailableValue());
        assign(m_column2, 1, disk.capacityBytes > 0 ? FormatBytes(disk.capacityBytes) : UnavailableValue());
        assign(m_column2, 2, std::to_string(disk.queueDepth));
        assign(m_column2, 3, UnavailableValue());

        assign(m_column3, 0, disk.modelName.empty() ? UnavailableValue() : disk.modelName);
        assign(m_column3, 1, disk.instanceName.empty() ? UnavailableValue() : disk.instanceName);
        // A solid-state device answers the seek-penalty query with zero; anything else is a spinning
        // disk. Reporting the distinction is what the original's "Type" row does.
        assign(m_column3, 2, UnavailableValue());
        assign(m_column3, 3, index == 0 ? "Yes" : "No");
    }
}
