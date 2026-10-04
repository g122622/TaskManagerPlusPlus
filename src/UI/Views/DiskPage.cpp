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
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // heading
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // active-time caption
        m_root.RowDefinitions().Append(controls::MakeStarRow()); // active-time chart
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // transfer caption
        m_root.RowDefinitions().Append(controls::MakeStarRow()); // transfer chart
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // details

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

        // --- Active-time caption -----------------------------------------------
        Grid captionRow = Grid();
        captionRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        captionRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
        captionRow.Margin(ThicknessHelper::FromLengths(0.0, 10.0, 0.0, 4.0));

        m_activeCaption = controls::MakeText(L"Active time", 12.0, true);
        m_activeCaption.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(m_activeCaption, 0);
        captionRow.Children().Append(m_activeCaption);

        TextBlock maximumLabel = controls::MakeText(L"100%", 12.0, true);
        maximumLabel.HorizontalAlignment(HorizontalAlignment::Right);
        Grid::SetColumn(maximumLabel, 1);
        captionRow.Children().Append(maximumLabel);

        Grid::SetRow(captionRow, 1);
        m_root.Children().Append(captionRow);

        // --- Active-time chart -------------------------------------------------
        //
        // A share of one device, so the axis is a real proportion and 100 percent means fully busy.
        m_activeChart = std::make_unique<HistoryChart>(L"", DEFAULT_DISK_COLOR, 100.0);
        m_activeChart->SetHeaderVisible(false);
        m_activeChart->Root().MinHeight(CHART_MIN_HEIGHT);

        Grid::SetRow(m_activeChart->Root(), 2);
        m_root.Children().Append(m_activeChart->Root());

        // --- Transfer-rate caption ---------------------------------------------
        Grid transferCaptionRow = Grid();
        transferCaptionRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        transferCaptionRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
        transferCaptionRow.Margin(ThicknessHelper::FromLengths(0.0, 12.0, 0.0, 4.0));

        m_transferCaption = controls::MakeText(L"Disk transfer rate", 12.0, true);
        m_transferCaption.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(m_transferCaption, 0);
        transferCaptionRow.Children().Append(m_transferCaption);

        // The top of this axis is scaled to the data, so the figure at the top has to be stated:
        // unlike the active-time chart, "100%" would be meaningless here.
        m_transferPeakLabel = controls::MakeText(L"", 12.0, true);
        m_transferPeakLabel.HorizontalAlignment(HorizontalAlignment::Right);
        Grid::SetColumn(m_transferPeakLabel, 1);
        transferCaptionRow.Children().Append(m_transferPeakLabel);

        Grid::SetRow(transferCaptionRow, 3);
        m_root.Children().Append(transferCaptionRow);

        // --- Transfer-rate chart -----------------------------------------------
        m_transferChart = std::make_unique<HistoryChart>(L"", DEFAULT_DISK_COLOR, 100.0);
        m_transferChart->SetHeaderVisible(false);
        m_transferChart->Root().MinHeight(CHART_MIN_HEIGHT);

        Grid::SetRow(m_transferChart->Root(), 4);
        m_root.Children().Append(m_transferChart->Root());

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

        Grid::SetRow(details, 5);
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
        if (m_activeChart != nullptr)
        {
            m_activeChart->SetLineColor(color);
        }
        if (m_transferChart != nullptr)
        {
            m_transferChart->SetLineColor(color);
        }
    }

    void DiskPage::SetLineWidth(double width)
    {
        if (m_activeChart != nullptr)
        {
            m_activeChart->SetLineWidth(width);
        }
        if (m_transferChart != nullptr)
        {
            m_transferChart->SetLineWidth(width);
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

        // --- Active time -------------------------------------------------------
        //
        // The figure in the caption is the live reading; the chart behind it is the mean across every
        // device, and its axis is a real proportion so 100 percent means fully busy.
        size_t const deviceIndex = (m_deviceIndex < system.disks.size()) ? m_deviceIndex : 0;
        double const activePercent = (deviceIndex < system.disks.size())
                                         ? system.disks[deviceIndex].activePercent
                                         : system.diskActivePercent;

        m_activeCaption.Text(winrt::to_hstring("Active time   " + FormatPercent(activePercent) +
                                               "   \xE2\x80\xA2   over " +
                                               std::to_string(m_coordinator.HistorySeconds()) + " s"));

        // The device's own history, looked up by instance name.
        //
        // This chart previously plotted the machine-wide aggregate for every device, which is why every
        // disk showed the same curve: the aggregate is the machine's total, not the open device's. The
        // per-device series exists and is what the page must plot.
        std::vector<double> const* deviceRead = nullptr;
        std::vector<double> const* deviceWrite = nullptr;

        if (deviceIndex < system.disks.size())
        {
            auto const found = history.diskBytesPerSecondByDevice.find(system.disks[deviceIndex].instanceName);
            if (found != history.diskBytesPerSecondByDevice.end())
            {
                deviceRead = &found->second.first;
                deviceWrite = &found->second.second;
            }
        }

        // The active-time chart plots total throughput, scaled to its own peak: the model keeps a
        // throughput series per device rather than an active-time one, and deriving a percentage from
        // bytes would be inventing a figure. The shape is what this chart is for.
        ChartSeries activeSeries;
        if (deviceRead != nullptr && deviceWrite != nullptr)
        {
            activeSeries.values = *deviceRead;
            for (size_t i = 0; i < activeSeries.values.size() && i < deviceWrite->size(); ++i)
            {
                activeSeries.values[i] += (*deviceWrite)[i];
            }
        }
        activeSeries.windowSamples = history.windowSamples;

        double activePeak = 0.0;
        for (double const value : activeSeries.values)
        {
            activePeak = (std::max)(activePeak, value);
        }

        m_activeChart->SetMaximum(activePeak > 0.0 ? activePeak : 1.0);
        m_activeChart->SetSeries(activeSeries);

        // --- Transfer rate -----------------------------------------------------
        //
        // Scaled to the busiest sample in the window, and the top of the axis is stated above it so
        // the reader knows what the height means.
        // Reads solid, writes dashed, on one axis. Both are bytes per second so they share a scale,
        // and comparing them is the point of the chart.
        ChartSeries readSeries;
        if (deviceRead != nullptr)
        {
            readSeries.values = *deviceRead;
        }
        readSeries.windowSamples = history.windowSamples;

        ChartSeries writeSeries;
        if (deviceWrite != nullptr)
        {
            writeSeries.values = *deviceWrite;
        }
        writeSeries.windowSamples = history.windowSamples;

        // The axis is set from the busier of the two, so neither line is clipped.
        double transferPeak = 0.0;
        for (double const value : readSeries.values)
        {
            transferPeak = (std::max)(transferPeak, value);
        }
        for (double const value : writeSeries.values)
        {
            transferPeak = (std::max)(transferPeak, value);
        }
        transferPeak = (transferPeak > 0.0) ? transferPeak : 1.0;

        m_transferChart->SetMaximum(transferPeak);
        m_transferChart->SetSeries(readSeries);
        m_transferChart->SetSecondarySeries(writeSeries);

        m_transferPeakLabel.Text(winrt::to_hstring(FormatBytes(static_cast<uint64_t>(transferPeak)) + "/s"));

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

        // A sample with no devices is a failed read rather than a machine without disks. The previous
        // reading stands, so one bad frame does not blank the page.
        if (!system.disks.empty())
        {
            m_lastDisks = system.disks;
        }

        if (m_lastDisks.empty())
        {
            // Genuinely nothing has been read yet: a machine whose disks cannot report performance
            // data, or the frame before the first reading. A dash is honest; a zero would read as a
            // measurement that was taken.
            for (size_t i = 0; i < m_column1.size(); ++i)
            {
                assign(m_column1, i, UnavailableValue());
                assign(m_column2, i, UnavailableValue());
                assign(m_column3, i, UnavailableValue());
            }
            return;
        }

        size_t const index = (m_deviceIndex < m_lastDisks.size()) ? m_deviceIndex : 0;
        domain::DiskActivity const& disk = m_lastDisks[index];

        assign(m_column1, 0, FormatPercent(disk.activePercent));

        // The mean time to service a request, over the interval. A dash when nothing was requested:
        // reporting zero would read as an instant response rather than as no measurement.
        if (disk.averageResponseMs > 0.0)
        {
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%.2f ms", disk.averageResponseMs);
            assign(m_column1, 1, std::string{buffer});
        }
        else
        {
            assign(m_column1, 1, UnavailableValue());
        }

        assign(m_column1, 2, _rateText(disk.readBytesPerSecond));
        assign(m_column1, 3, _rateText(disk.writeBytesPerSecond));

        assign(m_column2, 0, disk.capacityBytes > 0 ? FormatBytes(disk.capacityBytes) : UnavailableValue());

        // The filesystem is the meaningful "formatted" fact: the raw capacity is already stated, and
        // repeating it here said nothing.
        assign(m_column2, 1, disk.fileSystem.empty() ? UnavailableValue() : disk.fileSystem);
        assign(m_column2, 2, std::to_string(disk.queueDepth));
        assign(m_column2, 3, disk.hostsPageFile ? "Yes" : "No");

        assign(m_column3, 0, disk.modelName.empty() ? UnavailableValue() : disk.modelName);
        assign(m_column3, 1, disk.instanceName.empty() ? UnavailableValue() : disk.instanceName);

        // The type comes from the device's own seek-penalty and bus queries rather than from guessing
        // at the model string, which is what the original's "Type" row reports.
        assign(m_column3, 2, disk.TypeName());

        // TRIM support is a solid-state capability and needs no extra query: it arrived with the
        // descriptor that named the device.
        assign(m_column3, 3, disk.trimEnabled ? "Yes" : "No");
    }
}
