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

    NetworkPage::NetworkPage(core::SamplingCoordinator& coordinator, core::ChartStyle const& style)
        : m_coordinator(coordinator)
    {
        _buildLayout();
        // Applied here rather than by the caller. The page is built lazily, on first selection, so there
        // is no point after construction at which the caller reliably knows to style it -- and a chart
        // left at its constructed defaults is the whole of what the user sees until the settings are next
        // touched.
        SetAccentColor(winrt::Windows::UI::Color{0xFF, style.red, style.green, style.blue});
        SetLineWidth(style.ClampedLineWidth());
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

        // The adapter's model, right-aligned and sitting on the heading's baseline, matching how the CPU
        // and disk pages name their own hardware. Trimmed rather than wrapped: the heading has to keep its
        // place, and a model name is long enough to push it off the row on a narrow window.
        m_adapterModel = controls::MakeText(L"", 13.0, true);
        m_adapterModel.HorizontalAlignment(HorizontalAlignment::Right);
        m_adapterModel.VerticalAlignment(VerticalAlignment::Bottom);
        m_adapterModel.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        m_adapterModel.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        m_adapterModel.Margin(ThicknessHelper::FromLengths(12.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_adapterModel, 1);
        headingRow.Children().Append(m_adapterModel);

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

        // The figure at the top of the axis, which is scaled to the busiest of the two directions rather
        // than to a fixed proportion. It is filled in on each refresh, so it starts empty rather than
        // showing a figure that is not yet true.
        //
        // It used to read "100%", which was wrong twice over: the axis is not a percentage, and the thing
        // it was measuring is throughput rather than a share of the link. The link's capacity is a
        // separate figure and belongs in the heading, where it is.
        m_peakLabel = controls::MakeText(L"", 12.0, true);
        m_peakLabel.HorizontalAlignment(HorizontalAlignment::Right);
        Grid::SetColumn(m_peakLabel, 1);
        captionRow.Children().Append(m_peakLabel);

        Grid::SetRow(captionRow, 1);
        m_root.Children().Append(captionRow);

        // --- Throughput chart --------------------------------------------------
        //
        // Plots bytes per second, both directions on one axis, so the two can be compared directly. The
        // top of the axis follows the data.
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
        //
        // The model is not among them. It names the device rather than measuring it, so it belongs on the
        // heading row where the device is named, and repeating it here spent a row of the panel on the one
        // thing above it already said.
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

        // The adapter's own history, looked up by name. The adapter's series is plotted rather than the
        // aggregate, so opening a different adapter shows that adapter's traffic.
        std::vector<double> const* received = nullptr;
        std::vector<double> const* sent = nullptr;

        if (m_adapterIndex < system.networks.size())
        {
            auto const found = history.networkBytesPerSecondByAdapter.find(system.networks[m_adapterIndex].adapterName);
            if (found != history.networkBytesPerSecondByAdapter.end())
            {
                received = &found->second.first;
                sent = &found->second.second;
            }
        }

        // Received solid, sent dashed, on one axis. Both are bytes per second so they share a scale, and
        // comparing them is the point of the chart.
        ChartSeries receiveSeries;
        if (received != nullptr)
        {
            receiveSeries.values = *received;
        }
        receiveSeries.windowSamples = history.windowSamples;

        ChartSeries sendSeries;
        if (sent != nullptr)
        {
            sendSeries.values = *sent;
        }
        sendSeries.windowSamples = history.windowSamples;

        // Scaled to the busier of the two, so neither line is clipped.
        double peak = 0.0;
        for (double const value : receiveSeries.values)
        {
            peak = (std::max)(peak, value);
        }
        for (double const value : sendSeries.values)
        {
            peak = (std::max)(peak, value);
        }

        // The axis maximum and the figure stated at the top of it come from the same value, so the label
        // cannot claim something the plot does not show.
        //
        // Formatted through the same helper the caption and the detail rows use, so the figure at the top
        // of the axis reads in the same units and to the same precision as the readings beneath it.
        m_peakLabel.Text(winrt::to_hstring(_rateText(peak)));

        // An axis of zero would divide by it in the plotting arithmetic. One byte per second is the
        // smallest range that leaves the plot empty, which is the honest picture of no traffic.
        m_chart->SetMaximum(peak > 0.0 ? peak : 1.0);

        // A reference line at a third of the axis, so the curve can be read against a division of the scale
        // rather than against the frame alone. Stated in the same units as the readings, because the axis is
        // scaled to the data and its top is a byte rate rather than a round number.
        //
        // Set after the maximum, so the line is placed against the axis it belongs to. An empty label hides
        // it, which is what happens before anything has been transferred: there is no third of a peak to
        // name.
        if (peak > 0.0)
        {
            m_chart->SetReferenceLine(1.0 / 3.0, winrt::to_hstring(_rateText(peak / 3.0)));
        }
        else
        {
            m_chart->SetReferenceLine(1.0 / 3.0, L"");
        }

        m_chart->SetSeries(receiveSeries);
        m_chart->SetSecondarySeries(sendSeries);

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

        // The column's rows shifted down when the model left it, so these follow the labels above.
        assign(m_column2, 0, iface.connected ? "Connected" : "Disconnected");
        assign(m_column2, 1, _linkSpeedText(iface.receiveLinkSpeedBps));
        assign(m_column2, 2, _linkSpeedText(iface.transmitLinkSpeedBps));

        // The error counters are running totals, which is the form they are meaningful in: a
        // per-second rate would report zero for a healthy link and imply a fault rate that does not
        // exist. The discards are shown alongside, since a dropped packet under load and a corrupt
        // one are different problems.
        assign(m_column3, 0, FormatCount(iface.sendErrors) + "  (" + FormatCount(iface.sendDiscards) + " dropped)");
        assign(m_column3, 1, FormatCount(iface.receiveErrors) + "  (" + FormatCount(iface.receiveDiscards) + " dropped)");
        assign(m_column3, 2, iface.connected ? "Connected" : "Disconnected");
        assign(m_column3, 3, iface.virtualAdapter ? "Yes" : "No");

        if (m_adapterModel != nullptr)
        {
            m_adapterModel.Text(winrt::to_hstring(iface.adapterName.empty() ? UnavailableValue()
                                                                           : iface.adapterName));
        }
    }
}
