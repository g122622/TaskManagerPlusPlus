#include "UI/WinRTUI.h"

#include "UI/PerformanceView.h"

#include "UI/Controls.h"
#include "UI/Formatting.h"
#include "UI/Theme.h"

#include <algorithm>
#include <string>
#include <vector>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::Button;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        /// Height of a sidebar row. Tall enough for a sparkline beside the label.
        constexpr double SIDEBAR_ROW_HEIGHT = 52.0;

        /// Sparkline size inside a sidebar row.
        constexpr double SPARKLINE_WIDTH = 72.0;
        constexpr double SPARKLINE_HEIGHT = 26.0;

        /// Height of the primary chart's cell. A minimum keeps the curve meaningful
        /// when the window is short; the cell grows past it with the window.
        constexpr double PRIMARY_CHART_MIN_HEIGHT = 180.0;

        /// Height of the secondary chart's cell.
        constexpr double SECONDARY_CHART_MIN_HEIGHT = 120.0;

        /**
         * @brief Row labels for a section's details card.
         *
         * Declared as a table so the labels and the value assignments in
         * _updateDetails cannot drift apart: both are written against this list, so a
         * mismatch shows up as a blank row rather than as silently wrong data.
         */
        [[nodiscard]] std::vector<wchar_t const*> _detailLabelsFor(int sectionIndex)
        {
            constexpr int CPU_SECTION = 0;
            constexpr int MEMORY_SECTION = 1;

            switch (sectionIndex)
            {
                case CPU_SECTION:
                    return {L"Utilisation", L"Logical processors", L"Cores", L"Architecture"};
                case MEMORY_SECTION:
                    return {L"In use", L"Available", L"Total", L"Committed"};
                default:
                    return {};
            }
        }

        /**
         * @brief Builds one label/value row and returns the value block for updates.
         */
        [[nodiscard]] Grid _makeDetailRow(wchar_t const* label, TextBlock& outValue)
        {
            Grid row = Grid();
            row.ColumnDefinitions().Append(ColumnDefinition{});
            row.ColumnDefinitions().Append(ColumnDefinition{});
            row.Margin(ThicknessHelper::FromLengths(0.0, 3.0, 0.0, 3.0));

            TextBlock labelBlock = controls::MakeText(label, 13.0, true);

            outValue = controls::MakeText(L"\x2014", 13.0);
            outValue.HorizontalAlignment(HorizontalAlignment::Right);
            outValue.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);

            Grid::SetColumn(labelBlock, 0);
            Grid::SetColumn(outValue, 1);
            row.Children().Append(labelBlock);
            row.Children().Append(outValue);
            return row;
        }
    }

    std::vector<PerformanceView::SectionSpec> const& PerformanceView::_sections()
    {
        // The colours are the hook the colour-customisation feature will drive
        // (docs/ROADMAP.md, M1-6); today they are fixed and chosen to stay legible in
        // both light and dark themes.
        static std::vector<SectionSpec> const sections{
            {L"CPU", L"\xE950", true, winrt::Windows::UI::Color{0xFF, 0x4C, 0xC2, 0xFF}},
            {L"Memory", L"\xEEA0", true, winrt::Windows::UI::Color{0xFF, 0x9B, 0x8C, 0xFF}},
            {L"Disk", L"\xEDA2", false, winrt::Windows::UI::Color{0xFF, 0x6E, 0xD8, 0xB0}},
            {L"Network", L"\xE968", false, winrt::Windows::UI::Color{0xFF, 0xFF, 0xC1, 0x57}},
            {L"GPU", L"\xE7F4", false, winrt::Windows::UI::Color{0xFF, 0xFF, 0x8A, 0xA8}},
        };
        return sections;
    }

    PerformanceView::PerformanceView(core::SamplingCoordinator& coordinator) : m_coordinator(coordinator)
    {
        _buildLayout();
        _selectSection(Section::Cpu);
    }

    void PerformanceView::_buildLayout()
    {
        m_root = Grid();
        m_root.Padding(ThicknessHelper::FromLengths(metrics::PAGE_MARGIN, 12.0, metrics::PAGE_MARGIN, 8.0));

        // Two columns: the sidebar at a fixed width, and the detail area taking
        // everything else. A star column is the equivalent of CSS calc(100% - 240px)
        // and, unlike a hard-coded width, it keeps working when the window is resized
        // or the sidebar width changes.
        ColumnDefinition sidebarColumn;
        sidebarColumn.Width(winrt::Microsoft::UI::Xaml::GridLength{metrics::NAVIGATION_PANE_WIDTH});
        m_root.ColumnDefinitions().Append(sidebarColumn);

        ColumnDefinition detailColumn;
        detailColumn.Width(winrt::Microsoft::UI::Xaml::GridLength{1.0, GridUnitType::Star});
        m_root.ColumnDefinitions().Append(detailColumn);

        // --- Sidebar -----------------------------------------------------------
        //
        // No ScrollViewer: the sidebar holds a fixed five rows that always fit, and a
        // ScrollViewer here would give its content unlimited height, which is exactly
        // what made star sizing fail elsewhere on this page.
        Border sidebarCard = controls::MakeCard();
        sidebarCard.Padding(ThicknessHelper::FromLengths(4.0, 4.0, 4.0, 4.0));
        sidebarCard.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 16.0, 0.0));
        sidebarCard.VerticalAlignment(VerticalAlignment::Top);
        sidebarCard.HorizontalAlignment(HorizontalAlignment::Stretch);

        m_sidebar = controls::MakeStack(2.0);

        for (size_t i = 0; i < _sections().size(); ++i)
        {
            SectionSpec const& spec = _sections()[i];
            Section const section = static_cast<Section>(i);

            Grid rowContent = Grid();
            rowContent.ColumnDefinitions().Append(ColumnDefinition{});

            ColumnDefinition sparkColumn;
            sparkColumn.Width(winrt::Microsoft::UI::Xaml::GridLength{0.0, GridUnitType::Auto});
            rowContent.ColumnDefinitions().Append(sparkColumn);

            // Left cell: icon, title, and a note when the metric is not collected yet.
            StackPanel label = controls::MakeRow(10.0);
            label.VerticalAlignment(VerticalAlignment::Center);

            winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
            icon.Glyph(spec.glyph);
            icon.FontSize(15.0);
            label.Children().Append(icon);

            TextBlock title = controls::MakeText(spec.title, 14.0);
            title.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            label.Children().Append(title);
            m_rowTitles.push_back(title);

            Grid::SetColumn(label, 0);
            rowContent.Children().Append(label);

            // Right cell: a sparkline for every row, including the ones whose metrics
            // are not collected. Those stay empty, which reads as "no data" rather than
            // as a flat measurement of zero.
            auto sparkline = std::make_unique<Sparkline>(spec.color, SPARKLINE_WIDTH, SPARKLINE_HEIGHT);
            sparkline->SetColors(spec.color, /*muted=*/true);
            sparkline->Root().HorizontalAlignment(HorizontalAlignment::Right);
            sparkline->Root().VerticalAlignment(VerticalAlignment::Center);

            Grid::SetColumn(sparkline->Root(), 1);
            rowContent.Children().Append(sparkline->Root());
            m_rowSparklines.push_back(std::move(sparkline));

            Button button;
            button.Content(rowContent);
            button.Height(SIDEBAR_ROW_HEIGHT);
            button.Padding(ThicknessHelper::FromLengths(10.0, 0.0, 10.0, 0.0));
            button.HorizontalAlignment(HorizontalAlignment::Stretch);
            button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            button.Background(
                winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
            button.BorderThickness(ThicknessHelper::FromUniformLength(0.0));

            button.Click([this, section](winrt::Windows::Foundation::IInspectable const&,
                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
                _selectSection(section);
            });

            m_buttons.push_back(button);
            m_rows.push_back(rowContent);
            m_sidebar.Children().Append(button);
        }

        sidebarCard.Child(m_sidebar);
        Grid::SetColumn(sidebarCard, 0);
        m_root.Children().Append(sidebarCard);

        // --- Detail area -------------------------------------------------------
        //
        // A plain Grid, not a ScrollViewer. The charts take star rows so they grow with
        // the window, and the details card takes an Auto row at the bottom so it is
        // always visible at its natural height. Wrapping this in a ScrollViewer would
        // give the star rows unlimited height, collapsing the charts and introducing a
        // scrollbar to reach content that should have been on screen.
        m_detailHost = Grid();
        m_detailHost.RowDefinitions().Append(RowDefinition{}); // primary chart
        m_detailHost.RowDefinitions().Append(RowDefinition{}); // secondary chart
        m_detailHost.RowDefinitions().Append(RowDefinition{}); // details card

        Grid::SetColumn(m_detailHost, 1);
        m_root.Children().Append(m_detailHost);
    }

    void PerformanceView::_updateSelectionVisuals()
    {
        for (size_t i = 0; i < m_buttons.size(); ++i)
        {
            bool const selected = (static_cast<int>(m_selected) == static_cast<int>(i));

            // The selected row is filled with a translucent accent, which is how the
            // Task Manager rail reads. Its sparkline goes to full opacity so the
            // current section's trend is the one that stands out.
            m_buttons[i].Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                selected ? winrt::Windows::UI::Color{0x33, 0x4C, 0xC2, 0xFF} : winrt::Windows::UI::Colors::Transparent()});
            m_buttons[i].CornerRadius(
                winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(metrics::CONTROL_RADIUS));

            if (i < m_rowSparklines.size())
            {
                m_rowSparklines[i]->SetColors(_sections()[i].color, /*muted=*/!selected);
            }
        }
    }

    void PerformanceView::_selectSection(Section section)
    {
        m_selected = section;
        m_detailHost.Children().Clear();
        m_primaryChart.reset();
        m_secondaryChart.reset();
        m_detailsCard = nullptr;
        m_detailValues.clear();

        _updateSelectionVisuals();

        auto const index = static_cast<size_t>(section);
        bool const hasData = (index < _sections().size()) && _sections()[index].hasData;

        if (hasData)
        {
            auto const& spec = _sections()[index];

            // Primary chart: takes the most space, since it is the one being read.
            m_primaryChart = std::make_unique<HistoryChart>(
                section == Section::Cpu ? L"CPU utilisation" : L"Memory usage", spec.color, 100.0);
            m_primaryChart->Root().MinHeight(PRIMARY_CHART_MIN_HEIGHT);
            Grid::SetRow(m_primaryChart->Root(), 0);
            m_detailHost.Children().Append(m_primaryChart->Root());

            if (section == Section::Cpu)
            {
                // Per-logical-processor view, drawn with the same component so the
                // colour customisation will cover both.
                m_secondaryChart = std::make_unique<HistoryChart>(L"Logical processors", spec.color, 100.0);
                m_secondaryChart->Root().MinHeight(SECONDARY_CHART_MIN_HEIGHT);
                Grid::SetRow(m_secondaryChart->Root(), 1);
                m_detailHost.Children().Append(m_secondaryChart->Root());
            }

            // Details card, created once per section. Its value blocks are retained so a
            // refresh writes into existing controls instead of adding new ones several
            // times a second.
            m_detailsCard = controls::MakeCard();
            m_detailsCard.Margin(ThicknessHelper::FromLengths(0.0, 12.0, 0.0, 0.0));
            m_detailsCard.VerticalAlignment(VerticalAlignment::Bottom);

            StackPanel content = controls::MakeStack(2.0);
            for (wchar_t const* label : _detailLabelsFor(static_cast<int>(section)))
            {
                TextBlock value{nullptr};
                content.Children().Append(_makeDetailRow(label, value));
                m_detailValues.push_back(value);
            }

            {
                TextBlock value{nullptr};
                content.Children().Append(_makeDetailRow(L"Sampling interval", value));
                m_detailValues.push_back(value);
            }

            m_detailsCard.Child(content);
            Grid::SetRow(m_detailsCard, 2);
            m_detailHost.Children().Append(m_detailsCard);
        }
        else
        {
            // These probes do not exist yet. Saying so is better than an empty chart,
            // which would read as "measured zero".
            Border card = controls::MakeCard();
            StackPanel content = controls::MakeStack(6.0);
            content.Children().Append(controls::MakeHeading(L"Not collected yet", 16.0));
            content.Children().Append(controls::MakeText(
                L"This section's metrics are not implemented yet. The collection path is described "
                L"in docs/METRICS.md and the work is listed in docs/ROADMAP.md.",
                13.0,
                true));
            card.Child(content);
            Grid::SetRow(card, 0);
            m_detailHost.Children().Append(card);
        }

        // Force the next refresh to populate the freshly built controls.
        m_renderedVersion = 0;
    }

    void PerformanceView::_updateSidebarCharts(domain::HistoryView const& history)
    {
        // Every row gets a series, so selecting a section does not change which rows
        // have charts. Rows without a probe keep an explicit empty series rather than
        // being skipped, so the difference between "no data" and "not collected" is
        // visible in the sidebar.
        for (size_t i = 0; i < m_rowSparklines.size(); ++i)
        {
            auto const index = static_cast<size_t>(i);
            if (index >= _sections().size() || !_sections()[index].hasData)
            {
                m_rowSparklines[i]->Clear();
                continue;
            }

            if (static_cast<Section>(i) == Section::Cpu)
            {
                m_rowSparklines[i]->SetSeries(history.cpuTotal, 100.0);
            }
            else if (static_cast<Section>(i) == Section::Memory)
            {
                m_rowSparklines[i]->SetSeries(history.memoryUsed, 100.0);
            }
        }
    }

    void PerformanceView::Refresh()
    {
        // Version first, for the same reason as the process list: the copies below are
        // far more expensive than comparing a number, and the UI polls much more often
        // than the sampler publishes.
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        _updateDetails(system);
        _updateSidebarCharts(history);

        if (m_primaryChart != nullptr)
        {
            if (m_selected == Section::Cpu)
            {
                m_primaryChart->SetSeries(history.cpuTotal);
                m_primaryChart->SetCurrentValueText(system.ratesUnavailable
                                                        ? winrt::to_hstring(UnavailableValue())
                                                        : winrt::to_hstring(FormatPercent(system.cpuPercent)));
            }
            else if (m_selected == Section::Memory)
            {
                m_primaryChart->SetSeries(history.memoryUsed);
                m_primaryChart->SetCurrentValueText(winrt::to_hstring(FormatBytes(system.memoryUsedBytes)));
            }
        }

        if (m_secondaryChart != nullptr && m_selected == Section::Cpu)
        {
            m_secondaryChart->SetSeries(system.perProcessorCpuPercent);

            size_t const busyCores = static_cast<size_t>(std::count_if(
                system.perProcessorCpuPercent.begin(),
                system.perProcessorCpuPercent.end(),
                [](double value) { return value > 1.0; }));
            m_secondaryChart->SetCurrentValueText(winrt::hstring{std::to_wstring(busyCores) + L" / " + std::to_wstring(system.perProcessorCpuPercent.size()) + L" busy"});
        }
    }

    void PerformanceView::_updateDetails(domain::SystemView const& system)
    {
        if (m_detailValues.empty())
        {
            return;
        }

        platform::SystemProcessorInfo const& processor = system.processor;

        // The value blocks were created in the same order as the labels, so they are
        // assigned positionally. The last block is always the sampling interval.
        //
        // The lambda takes UTF-8 because that is the project's internal string type
        // (the Formatting helpers return std::string); converting once here is cheaper
        // and less error-prone than converting at every call site.
        size_t index = 0;
        auto assign = [this, &index](std::string const& utf8) {
            if (index < m_detailValues.size())
            {
                m_detailValues[index++].Text(winrt::to_hstring(utf8));
            }
        };

        switch (m_selected)
        {
            case Section::Cpu:
                assign(FormatPercent(system.cpuPercent));
                assign(std::to_string(processor.logicalProcessorCount));
                assign(std::to_string(processor.physicalCoreCount));
                assign(processor.architecture.empty() ? std::string{"unknown"} : processor.architecture);
                break;

            case Section::Memory:
                assign(FormatBytes(system.memoryUsedBytes));
                assign(FormatBytes(system.memory.availablePhysical));
                assign(FormatBytes(system.memory.totalPhysical));
                assign(FormatBytes(system.memory.totalPageFile));
                break;

            default:
                break;
        }

        assign(std::to_string(m_coordinator.IntervalMs()) + " ms");
    }
}
