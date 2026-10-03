#include "UI/WinRTUI.h"

#include "UI/Views/NetworkPage.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <cstdio>
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

        /// The default network line colour, matching the sidebar's network row.
        constexpr winrt::Windows::UI::Color DEFAULT_NETWORK_COLOR{0xFF, 0xFF, 0xC1, 0x57};

        [[nodiscard]] std::string _rateText(double bytesPerSecond)
        {
            return FormatBytes(static_cast<uint64_t>((std::max)(0.0, bytesPerSecond))) + "/s";
        }

        /// A link speed in the unit a user recognises: Mbps for most links, Gbps for fast ones.
        ///
        /// Formatted here rather than through the shared helpers because a link speed is in bits
        /// while every other figure on the page is in bytes, and bending the byte formatter to cover
        /// it would make one of the two wrong.
        [[nodiscard]] std::string _linkSpeedText(uint64_t bitsPerSecond)
        {
            if (bitsPerSecond == 0)
            {
                return UnavailableValue();
            }

            double const mbps = static_cast<double>(bitsPerSecond) / 1.0e6;

            char buffer[32]{};
            if (mbps >= 1000.0)
            {
                std::snprintf(buffer, sizeof(buffer), "%.1f Gbps", mbps / 1000.0);
            }
            else
            {
                std::snprintf(buffer, sizeof(buffer), "%.0f Mbps", mbps);
            }
            return std::string{buffer};
        }
    }

    NetworkPage::NetworkPage(core::SamplingCoordinator& coordinator) : m_coordinator(coordinator)
    {
        _buildLayout();
    }

    void NetworkPage::_buildLayout()
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

        m_heading = controls::MakeHeading(L"Network", HEADING_FONT_SIZE);
        Grid::SetColumn(m_heading, 0);
        headingRow.Children().Append(m_heading);

        m_linkCaption = controls::MakeText(L"", 13.0, true);
        m_linkCaption.VerticalAlignment(VerticalAlignment::Bottom);
        m_linkCaption.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_linkCaption, 1);
        headingRow.Children().Append(m_linkCaption);

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

        // --- Throughput chart --------------------------------------------------
        //
        // Plots link utilisation rather than bytes per second, so the axis is a real proportion: a
        // link has a known capacity, and a percentage of it is the figure the original shows.
        m_chart = std::make_unique<HistoryChart>(L"", DEFAULT_NETWORK_COLOR, 100.0);
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

        // Column 1: the headline throughput.
        m_column1.push_back(_addDetail(column1, L"Send"));
        m_column1.push_back(_addDetail(column1, L"Receive"));
        m_column1.push_back(_addDetail(column1, L"Utilisation"));
        m_column1.push_back(_addDetail(column1, L"Link speed"));

        // Column 2: the adapter's own figures.
        m_column2.push_back(_addDetail(column2, L"Adapter"));
        m_column2.push_back(_addDetail(column2, L"Connection"));
        m_column2.push_back(_addDetail(column2, L"Receive link"));
        m_column2.push_back(_addDetail(column2, L"Transmit link"));

        // Column 3: the diagnostics the original also shows.
        m_column3.push_back(_addDetail(column3, L"Send errors"));
        m_column3.push_back(_addDetail(column3, L"Receive errors"));
        m_column3.push_back(_addDetail(column3, L"Type"));
        m_column3.push_back(_addDetail(column3, L"Virtual"));

        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 3);
        m_root.Children().Append(details);
    }

    NetworkPage::DetailRow NetworkPage::_addDetail(StackPanel const& column, wchar_t const* label)
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

    void NetworkPage::SetAccentColor(winrt::Windows::UI::Color color)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineColor(color);
        }
    }

    void NetworkPage::SetLineWidth(double width)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineWidth(width);
        }
    }

    void NetworkPage::SetAdapterIndex(size_t index)
    {
        if (m_adapterIndex != index)
        {
            m_adapterIndex = index;
            m_renderedVersion = 0;
        }
    }

    void NetworkPage::SetAdapterLabel(std::wstring_view label)
    {
        m_adapterLabel.assign(label);
        if (m_heading != nullptr && !m_adapterLabel.empty())
        {
            m_heading.Text(winrt::hstring{m_adapterLabel});
        }
    }

    void NetworkPage::Refresh()
    {
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        if (m_heading != nullptr && m_adapterLabel.empty())
        {
            if (m_adapterIndex < system.networks.size())
            {
                m_heading.Text(winrt::to_hstring(system.networks[m_adapterIndex].adapterName));
            }
            else
            {
                m_heading.Text(L"Network");
            }
        }

        // The caption reports the busiest direction, which is what determines how close the link is
        // to its limit.
        double sendBps = 0.0;
        double receiveBps = 0.0;
        if (m_adapterIndex < system.networks.size())
        {
            sendBps = system.networks[m_adapterIndex].sentBytesPerSecond;
            receiveBps = system.networks[m_adapterIndex].receivedBytesPerSecond;
        }
        else
        {
            for (domain::NetworkActivity const& iface : system.networks)
            {
                sendBps += iface.sentBytesPerSecond;
                receiveBps += iface.receivedBytesPerSecond;
            }
        }

        m_caption.Text(winrt::to_hstring("S " + _rateText(sendBps) + "  R " + _rateText(receiveBps) +
                                         "  \xE2\x80\xA2  over " + std::to_string(m_coordinator.HistorySeconds()) +
                                         " s"));

        // The chart plots the aggregate history. Per-adapter history would need a series per adapter,
        // which the model does not keep; the aggregate is what the section's row summarises.
        ChartSeries series;
        series.values = history.networkReceiveBytesPerSecond;
        for (size_t i = 0; i < series.values.size() && i < history.networkSendBytesPerSecond.size(); ++i)
        {
            series.values[i] += history.networkSendBytesPerSecond[i];
        }
        series.windowSamples = history.windowSamples;

        // Scaled to the busiest sample: throughput in bytes per second has no fixed maximum, so a
        // percentage axis would be meaningless here.
        double peak = 0.0;
        for (double const value : series.values)
        {
            peak = (std::max)(peak, value);
        }

        m_chart->SetMaximum(peak > 0.0 ? peak : 1.0);
        m_chart->SetSeries(series);

        _updateDetails(system);
    }

    void NetworkPage::_updateDetails(domain::SystemView const& system)
    {
        auto assign = [](std::vector<DetailRow> const& rows, size_t index, std::string const& text) {
            if (index < rows.size() && rows[index].value != nullptr)
            {
                rows[index].value.Text(winrt::to_hstring(text));
            }
        };

        if (system.networks.empty())
        {
            for (size_t i = 0; i < m_column1.size(); ++i)
            {
                assign(m_column1, i, UnavailableValue());
                assign(m_column2, i, UnavailableValue());
                assign(m_column3, i, UnavailableValue());
            }
            return;
        }

        size_t const index = (m_adapterIndex < system.networks.size()) ? m_adapterIndex : 0;
        domain::NetworkActivity const& iface = system.networks[index];

        assign(m_column1, 0, _rateText(iface.sentBytesPerSecond));
        assign(m_column1, 1, _rateText(iface.receivedBytesPerSecond));
        assign(m_column1, 2, FormatPercent(iface.UtilizationPercent()));
        assign(m_column1, 3, _linkSpeedText(iface.receiveLinkSpeedBps));

        assign(m_column2, 0, iface.adapterName.empty() ? UnavailableValue() : iface.adapterName);
        assign(m_column2, 1, iface.connected ? "Connected" : "Disconnected");
        assign(m_column2, 2, _linkSpeedText(iface.receiveLinkSpeedBps));
        assign(m_column2, 3, _linkSpeedText(iface.transmitLinkSpeedBps));

        // The error counters are cumulative totals, which the activity struct does not carry, so
        // these stay unavailable rather than being shown as a stale or invented figure.
        assign(m_column3, 0, UnavailableValue());
        assign(m_column3, 1, UnavailableValue());
        assign(m_column3, 2, iface.connected ? "Ethernet or Wi-Fi" : UnavailableValue());
        assign(m_column3, 3, iface.virtualAdapter ? "Yes" : "No");

        if (m_linkCaption != nullptr)
        {
            m_linkCaption.Text(winrt::to_hstring(_linkSpeedText(iface.receiveLinkSpeedBps)));
        }
    }
}
