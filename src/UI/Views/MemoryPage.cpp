#include "UI/WinRTUI.h"

#include "UI/Views/MemoryPage.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <string>

using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
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
        /// Default series colour for memory, matching the sidebar swatch.
        constexpr winrt::Windows::UI::Color DEFAULT_MEMORY_COLOR{0xFF, 0x9B, 0x8C, 0xFF};

        /// Font size of the heading.
        constexpr double HEADING_FONT_SIZE = 22.0;

        /// Minimum chart height, so the curve stays readable in a short window.
        constexpr double CHART_MIN_HEIGHT = 220.0;
    }

    MemoryPage::MemoryPage(core::SamplingCoordinator& coordinator) : m_coordinator(coordinator)
    {
        _buildLayout();
    }

    void MemoryPage::_buildLayout()
    {
        m_root = Grid();

        // Rows: heading (Auto), caption (Auto), chart (star), details (Auto). The helpers
        // are required because a default-constructed RowDefinition is 1* (Star), which
        // would give every row an equal share instead of sizing the first ones to content.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeStarRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());

        // --- Heading -----------------------------------------------------------
        m_heading = controls::MakeHeading(winrt::to_hstring(UnavailableValue()), HEADING_FONT_SIZE);
        Grid::SetRow(m_heading, 0);
        m_root.Children().Append(m_heading);

        // --- Caption, matching the original's "In use (compressed) over 60 seconds" ----
        Grid captionRow = Grid();
        captionRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        captionRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
        captionRow.Margin(ThicknessHelper::FromLengths(0.0, 10.0, 0.0, 4.0));

        uint32_t const windowSeconds = m_coordinator.HistorySeconds();
        std::wstring caption = L"In use over ";
        if (windowSeconds >= 60 && windowSeconds % 60 == 0)
        {
            uint32_t const minutes = windowSeconds / 60;
            caption += std::to_wstring(minutes);
            caption += (minutes == 1) ? L" minute" : L" minutes";
        }
        else
        {
            caption += std::to_wstring(windowSeconds);
            caption += (windowSeconds == 1) ? L" second" : L" seconds";
        }

        m_caption = controls::MakeText(caption, 12.0, true);

        TextBlock maximumLabel = controls::MakeText(L"100%", 12.0, true);
        maximumLabel.HorizontalAlignment(HorizontalAlignment::Right);

        Grid::SetColumn(m_caption, 0);
        Grid::SetColumn(maximumLabel, 1);
        captionRow.Children().Append(m_caption);
        captionRow.Children().Append(maximumLabel);

        Grid::SetRow(captionRow, 1);
        m_root.Children().Append(captionRow);

        // --- Usage chart -------------------------------------------------------
        //
        // This is the chart that was missing: the memory section previously built only a
        // details card, so selecting it showed figures with no graph at all.
        m_chart = std::make_unique<HistoryChart>(L"", DEFAULT_MEMORY_COLOR, 100.0);
        m_chart->SetHeaderVisible(false);
        m_chart->Root().MinHeight(CHART_MIN_HEIGHT);

        Grid::SetRow(m_chart->Root(), 2);
        m_root.Children().Append(m_chart->Root());

        // --- Three-column details ----------------------------------------------
        Grid details = Grid();
        details.Margin(ThicknessHelper::FromLengths(0.0, 16.0, 0.0, 0.0));

        for (int i = 0; i < 3; ++i)
        {
            details.ColumnDefinitions().Append(controls::MakeStarColumn());
        }

        StackPanel column1 = controls::MakeStack(4.0);
        StackPanel column2 = controls::MakeStack(4.0);
        StackPanel column3 = controls::MakeStack(4.0);

        // A right-aligned value sits flush against its column edge, so each column is inset
        // on the right to keep one group's value from touching the next group's label.
        winrt::Microsoft::UI::Xaml::Thickness const padding =
            ThicknessHelper::FromLengths(0.0, 0.0, 24.0, 0.0);
        column1.Padding(padding);
        column2.Padding(padding);
        column3.Padding(padding);

        // Column 1: the headline figures, all of which are real readings.
        m_column1.push_back(_addDetail(column1, L"In use"));
        m_column1.push_back(_addDetail(column1, L"Available"));
        m_column1.push_back(_addDetail(column1, L"Committed"));

        // Column 2: totals and the page file.
        m_column2.push_back(_addDetail(column2, L"Total"));
        m_column2.push_back(_addDetail(column2, L"Cached"));
        m_column2.push_back(_addDetail(column2, L"Paged pool"));
        m_column2.push_back(_addDetail(column2, L"Non-paged pool"));

        // Column 3: hardware figures, which need a probe that does not exist yet.
        m_column3.push_back(_addDetail(column3, L"Speed"));
        m_column3.push_back(_addDetail(column3, L"Slots used"));
        m_column3.push_back(_addDetail(column3, L"Form factor"));
        m_column3.push_back(_addDetail(column3, L"Hardware reserved"));

        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 3);
        m_root.Children().Append(details);
    }

    MemoryPage::DetailRow MemoryPage::_addDetail(StackPanel const& column, wchar_t const* label)
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

        Grid::SetColumn(detail.label, 0);
        Grid::SetColumn(detail.value, 1);
        row.Children().Append(detail.label);
        row.Children().Append(detail.value);

        column.Children().Append(row);
        return detail;
    }

    void MemoryPage::SetAccentColor(winrt::Windows::UI::Color color)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineColor(color);
        }
    }

    void MemoryPage::Refresh()
    {
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        m_heading.Text(winrt::to_hstring(FormatBytes(system.memoryUsedBytes) + " (" +
                                         FormatPercent(system.memoryUsedPercent) + ")"));

        // The window rides with the data, so the chart anchors its samples to the right edge
        // instead of stretching them across the full width.
        ChartSeries series;
        series.values = history.memoryUsed;
        series.windowSamples = history.windowSamples;
        m_chart->SetSeries(series);

        _updateDetails(system);
    }

    void MemoryPage::_updateDetails(domain::SystemView const& system)
    {
        auto assign = [](std::vector<DetailRow> const& rows, size_t index, std::string const& text) {
            if (index < rows.size() && rows[index].value != nullptr)
            {
                rows[index].value.Text(winrt::to_hstring(text));
            }
        };

        // Committed is the page file in use, which is what the original labels it.
        uint64_t const committed = (system.memory.totalPageFile > system.memory.availablePageFile)
                                       ? (system.memory.totalPageFile - system.memory.availablePageFile)
                                       : 0;

        assign(m_column1, 0, FormatBytes(system.memoryUsedBytes));
        assign(m_column1, 1, FormatBytes(system.memory.availablePhysical));
        assign(m_column1, 2, FormatBytes(committed));

        assign(m_column2, 0, FormatBytes(system.memory.totalPhysical));

        // These need probes that do not exist yet. Saying so keeps the difference between
        // "nothing in use" and "not measured" visible.
        assign(m_column2, 1, UnavailableValue() + " (not collected yet)");
        assign(m_column2, 2, UnavailableValue() + " (not collected yet)");
        assign(m_column2, 3, UnavailableValue() + " (not collected yet)");

        for (size_t i = 0; i < m_column3.size(); ++i)
        {
            assign(m_column3, i, UnavailableValue() + " (not collected yet)");
        }
    }
}
