#include "UI/WinRTUI.h"

#include "UI/Views/PerformanceView.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::Button;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        /// Sparkline size inside a sidebar row.
        ///
        /// The aspect is about 2:3, matching the original's thumbnails, which are considerably
        /// less wide than the row they sit in. A wider thumbnail crowds the label beside it.
        constexpr double SPARKLINE_WIDTH = 56.0;
        constexpr double SPARKLINE_HEIGHT = 36.0;

        /// Accent fill for the selected sidebar row.
        constexpr winrt::Windows::UI::Color SELECTION_FILL{0x33, 0x4C, 0xC2, 0xFF};

        /// Caption for a section that has data but no page of its own yet.
        constexpr wchar_t const* SECTIONS_LABEL_UNKNOWN = L"Not built yet";
    }

    std::vector<PerformanceView::SectionSpec> const& PerformanceView::_sections()
    {
        // The colours are the hook the colour-customisation feature will drive
        // (docs/ROADMAP.md, M1-6); today they are fixed and chosen to stay legible in
        // both light and dark themes.
        static std::vector<SectionSpec> const sections{
            {L"CPU", L"\xE950", true, winrt::Windows::UI::Color{0xFF, 0x4C, 0xC2, 0xFF}},
            {L"Memory", L"\xEEA0", true, winrt::Windows::UI::Color{0xFF, 0x9B, 0x8C, 0xFF}},
            {L"Disk 0 (C:)", L"\xEDA2", false, winrt::Windows::UI::Color{0xFF, 0x6E, 0xD8, 0xB0}},
            {L"Ethernet", L"\xE968", false, winrt::Windows::UI::Color{0xFF, 0xFF, 0xC1, 0x57}},
            {L"GPU 0", L"\xE7F4", false, winrt::Windows::UI::Color{0xFF, 0xFF, 0x8A, 0xA8}},
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
        // A smaller left inset than a normal page: the navigation rail already separates the content
        // from the window edge, so the full page margin leaves a conspicuous gap before the sidebar
        // card.
        m_root.Padding(ThicknessHelper::FromLengths(metrics::CONTENT_LEFT_INSET, 8.0, metrics::PAGE_MARGIN, 8.0));

        // Two columns: the sidebar at a fixed width, and the detail area taking
        // everything else. A star column is the equivalent of calc(100% - 300px) and,
        // unlike a hard-coded width, keeps working when the window is resized.
        ColumnDefinition sidebarColumn;
        sidebarColumn.Width(GridLengthHelper::FromPixels(metrics::PERFORMANCE_SIDEBAR_WIDTH));
        m_root.ColumnDefinitions().Append(sidebarColumn);

        ColumnDefinition detailColumn;
        detailColumn.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        m_root.ColumnDefinitions().Append(detailColumn);

        // --- Sidebar -----------------------------------------------------------
        //
        // No ScrollViewer: five rows always fit, and a ScrollViewer here would give its
        // content unlimited height, which is what makes star sizing fail elsewhere.
        Border sidebarCard = controls::MakeCard();
        sidebarCard.Padding(ThicknessHelper::FromLengths(3.0, 3.0, 3.0, 3.0));
        sidebarCard.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 14.0, 0.0));
        sidebarCard.VerticalAlignment(VerticalAlignment::Top);
        sidebarCard.HorizontalAlignment(HorizontalAlignment::Stretch);

        m_sidebar = controls::MakeStack(1.0);

        for (size_t i = 0; i < _sections().size(); ++i)
        {
            SectionSpec const& spec = _sections()[i];
            Section const section = static_cast<Section>(i);

            // Row layout: the row's chart on the left, then the name and its qualifier
            // stacked. This is the arrangement the original uses, and it is what lets one
            // row carry a name, a current value and a trend without any of them crowding
            // the others.
            Grid rowContent = Grid();

            ColumnDefinition thumbColumn;
            thumbColumn.Width(GridLengthHelper::FromPixels(SPARKLINE_WIDTH));
            rowContent.ColumnDefinitions().Append(thumbColumn);

            ColumnDefinition textColumn;
            textColumn.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            rowContent.ColumnDefinitions().Append(textColumn);

            // Rows with no probe keep their chart empty, which reads as "no data" rather
            // than as a flat measurement of zero.
            auto sparkline = std::make_unique<Sparkline>(spec.color, SPARKLINE_WIDTH, SPARKLINE_HEIGHT);
            sparkline->Root().VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(sparkline->Root(), 0);
            rowContent.Children().Append(sparkline->Root());

            StackPanel text = controls::MakeStack(0.0);
            text.VerticalAlignment(VerticalAlignment::Center);
            text.Margin(ThicknessHelper::FromLengths(8.0, 0.0, 0.0, 0.0));

            TextBlock title = controls::MakeText(spec.title, 14.0);
            title.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);

            // The qualifier line carries the current reading, or says plainly that the
            // metric is not collected yet.
            TextBlock subtitle = controls::MakeText(spec.hasData ? L"" : L"-- not collected yet", 12.0, true);
            subtitle.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);

            text.Children().Append(title);
            text.Children().Append(subtitle);

            Grid::SetColumn(text, 1);
            rowContent.Children().Append(text);

            Button button;
            button.Content(rowContent);
            button.Height(metrics::SIDEBAR_ROW_HEIGHT);
            button.Padding(ThicknessHelper::FromLengths(6.0, 0.0, 6.0, 0.0));
            button.HorizontalAlignment(HorizontalAlignment::Stretch);
            button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            button.Background(SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            button.BorderThickness(ThicknessHelper::FromUniformLength(0.0));

            button.Click([this, section](winrt::Windows::Foundation::IInspectable const&,
                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
                _selectSection(section);
            });

            SidebarRow row;
            row.button = button;
            row.title = title;
            row.subtitle = subtitle;
            row.sparkline = std::move(sparkline);
            m_rows.push_back(std::move(row));

            m_sidebar.Children().Append(button);
        }

        sidebarCard.Child(m_sidebar);
        Grid::SetColumn(sidebarCard, 0);
        m_root.Children().Append(sidebarCard);

        // --- Detail area -------------------------------------------------------
        m_detailHost = Grid();
        Grid::SetColumn(m_detailHost, 1);
        m_root.Children().Append(m_detailHost);
    }

    void PerformanceView::_updateSelectionVisuals()
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            bool const selected = (static_cast<int>(m_selected) == static_cast<int>(i));

            // The selected row is filled with a translucent accent. Its sparkline goes to
            // full opacity and the others are muted, so the current section's trend is the
            // one that stands out.
            m_rows[i].button.Background(
                SolidColorBrush(selected ? SELECTION_FILL : winrt::Windows::UI::Colors::Transparent()));
            m_rows[i].button.CornerRadius(
                winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(metrics::CONTROL_RADIUS));

            if (m_rows[i].sparkline != nullptr && i < _sections().size())
            {
                m_rows[i].sparkline->SetColors(_sections()[i].color, /*muted=*/!selected);
            }
        }
    }

    void PerformanceView::_selectSection(Section section)
    {
        m_selected = section;
        m_detailHost.Children().Clear();
        m_detailsCard = nullptr;
        m_detailValues.clear();

        _updateSelectionVisuals();

        auto const index = static_cast<size_t>(section);
        bool const hasData = (index < _sections().size()) && _sections()[index].hasData;

        if (!hasData)
        {
            // These probes do not exist yet (docs/ROADMAP.md, M2/M3). Saying so is better
            // than an empty chart, which would read as "measured zero".
            Border card = controls::MakeCard();
            StackPanel content = controls::MakeStack(6.0);
            content.Children().Append(controls::MakeHeading(L"Not collected yet", 16.0));
            content.Children().Append(controls::MakeText(
                L"This section's metrics are not implemented yet. The collection path is described "
                L"in docs/METRICS.md and the work is listed in docs/ROADMAP.md.",
                13.0,
                true));
            card.Child(content);
            m_detailHost.Children().Append(card);

            m_renderedVersion = 0;
            return;
        }

        if (section == Section::Cpu)
        {
            // The CPU page is created once and reused. Rebuilding it would discard the
            // per-core charts it holds, which would flash empty on every reselection.
            if (m_cpuPage == nullptr)
            {
                m_cpuPage = std::make_unique<CpuPage>(m_coordinator);
            }
            m_detailHost.Children().Append(m_cpuPage->Root());
            m_renderedVersion = 0;
            return;
        }

        if (section == Section::Memory)
        {
            // The memory page carries its own usage chart. Building only a details card
            // here, as an earlier version did, left the section with figures and no graph.
            if (m_memoryPage == nullptr)
            {
                m_memoryPage = std::make_unique<MemoryPage>(m_coordinator);
            }
            m_detailHost.Children().Append(m_memoryPage->Root());
            m_renderedVersion = 0;
            return;
        }

        // Any future section that has data but no dedicated page yet gets a bare card, so
        // the page is never empty even before its content exists.
        m_detailsCard = controls::MakeCard();
        StackPanel details = controls::MakeStack(3.0);
        details.Children().Append(controls::MakeHeading(SECTIONS_LABEL_UNKNOWN, 16.0));

        m_detailsCard.Child(details);
        m_detailHost.Children().Append(m_detailsCard);

        m_renderedVersion = 0;
    }

    void PerformanceView::_updateSidebarValues(domain::SystemView const& system,
                                               domain::HistoryView const& history)
    {
        auto setSubtitle = [this](size_t index, std::string const& text) {
            if (index < m_rows.size() && m_rows[index].subtitle != nullptr)
            {
                m_rows[index].subtitle.Text(winrt::to_hstring(text));
            }
        };

        // CPU: load and current clock, with the aggregate trend.
        std::string cpuText;
        cpuText += system.ratesUnavailable ? UnavailableValue() : FormatPercent(system.cpuPercent);
        cpuText += "  ";
        cpuText += system.processorSpeed.available ? std::to_string(system.processorSpeed.currentMhz) + " MHz"
                                                   : UnavailableValue();
        setSubtitle(0, cpuText);
        if (!m_rows.empty() && m_rows[0].sparkline != nullptr)
        {
            ChartSeries series;
            series.values = history.cpuTotal;
            series.windowSamples = history.windowSamples;
            m_rows[0].sparkline->SetSeries(series, 100.0);
        }

        // Memory: usage against the total, which is what the original shows.
        std::string memoryText =
            FormatBytes(system.memoryUsedBytes) + " / " + FormatBytes(system.memory.totalPhysical);
        memoryText += " (" + FormatPercent(system.memoryUsedPercent) + ")";
        setSubtitle(1, memoryText);
        if (m_rows.size() > 1 && m_rows[1].sparkline != nullptr)
        {
            ChartSeries series;
            series.values = history.memoryUsed;
            series.windowSamples = history.windowSamples;
            m_rows[1].sparkline->SetSeries(series, 100.0);
        }

        // Sections without a probe keep the note set at construction, and their charts
        // stay clear so the difference between "idle" and "not measured" remains visible.
        for (size_t i = 2; i < m_rows.size(); ++i)
        {
            if (m_rows[i].sparkline != nullptr)
            {
                m_rows[i].sparkline->Clear();
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

        _updateSidebarValues(system, history);
        _updateDetails(system, history);

        if (m_cpuPage != nullptr && m_selected == Section::Cpu)
        {
            m_cpuPage->Refresh();
        }
        if (m_memoryPage != nullptr && m_selected == Section::Memory)
        {
            m_memoryPage->Refresh();
        }
    }

    void PerformanceView::_updateDetails(domain::SystemView const& system, domain::HistoryView const& history)
    {
        (void)history;

        if (m_detailValues.empty())
        {
            return;
        }

        // Values are written into the rows created by _selectSection. Adding rows per
        // refresh would append controls several times a second and grow without bound,
        // which is the defect this pattern exists to avoid.
        size_t index = 0;
        auto assign = [this, &index](std::string const& text) {
            if (index < m_detailValues.size())
            {
                m_detailValues[index++].Text(winrt::to_hstring(text));
            }
        };

        if (m_selected == Section::Memory)
        {
            assign(FormatBytes(system.memoryUsedBytes));
            assign(FormatBytes(system.memory.availablePhysical));
            assign(FormatBytes(system.memory.totalPhysical));
            assign(FormatBytes(system.memory.totalPageFile));
        }
    }
}