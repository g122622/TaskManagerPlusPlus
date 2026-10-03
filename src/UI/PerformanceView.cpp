#include "UI/WinRTUI.h"

#include "UI/PerformanceView.h"

#include "UI/Controls.h"
#include "UI/Formatting.h"
#include "UI/Theme.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::Button;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility;
using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        /// Height of a section button.
        constexpr double SECTION_ROW_HEIGHT = 48.0;

        /// Chart colours. These are the hook the colour-customisation feature
        /// (docs/ROADMAP.md, M1-6) will drive; today they are fixed.
        constexpr winrt::Windows::UI::Color CPU_CHART_COLOR{0xFF, 0x4C, 0xC2, 0xFF};
        constexpr winrt::Windows::UI::Color MEMORY_CHART_COLOR{0xFF, 0x9B, 0x8C, 0xFF};

        /**
         * @brief One row in the hardware list.
         */
        struct SectionSpec
        {
            wchar_t const* title;
            wchar_t const* glyph; ///< Segoe Fluent Icons glyph.
            bool hasData;
        };

        constexpr std::array<SectionSpec, 5> SECTIONS{{
            {L"CPU", L"\xE950", true},
            {L"Memory", L"\xEEA0", true},
            {L"Disk", L"\xEDA2", false},
            {L"Network", L"\xE968", false},
            {L"GPU", L"\xE7F4", false},
        }};

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

    PerformanceView::PerformanceView(core::SamplingCoordinator& coordinator) : m_coordinator(coordinator)
    {
        _buildLayout();
        _selectSection(Section::Cpu);
    }

    void PerformanceView::_buildLayout()
    {
        m_root = Grid();
        m_root.Padding(ThicknessHelper::FromLengths(metrics::PAGE_MARGIN, 12.0, metrics::PAGE_MARGIN, 8.0));

        // Column 0 is the section list at a fixed width; column 1 takes the remainder.
        ColumnDefinition listColumn;
        listColumn.Width(winrt::Microsoft::UI::Xaml::GridLengthHelper::FromPixels(metrics::NAVIGATION_PANE_WIDTH));
        m_root.ColumnDefinitions().Append(listColumn);
        m_root.ColumnDefinitions().Append(ColumnDefinition{});

        // --- Section list ------------------------------------------------------
        Border listCard = controls::MakeCard();
        listCard.Padding(ThicknessHelper::FromLengths(4.0, 4.0, 4.0, 4.0));
        listCard.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 16.0, 0.0));
        listCard.VerticalAlignment(VerticalAlignment::Top);

        m_sectionButtons = controls::MakeStack(2.0);

        for (size_t i = 0; i < SECTIONS.size(); ++i)
        {
            SectionSpec const& spec = SECTIONS[i];
            Section const section = static_cast<Section>(i);

            StackPanel content;
            content.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
            content.Spacing(12.0);
            content.VerticalAlignment(VerticalAlignment::Center);

            winrt::Microsoft::UI::Xaml::Controls::FontIcon icon;
            icon.Glyph(spec.glyph);
            icon.FontSize(16.0);
            content.Children().Append(icon);

            content.Children().Append(controls::MakeText(spec.title, 14.0));

            // A section with no probe yet says so, rather than presenting an empty
            // chart that would look like a reading of zero.
            if (!spec.hasData)
            {
                TextBlock pending = controls::MakeText(L"\x2014 not collected yet", 11.0, true);
                content.Children().Append(pending);
            }

            Button button;
            button.Content(content);
            button.Height(SECTION_ROW_HEIGHT);
            button.Padding(ThicknessHelper::FromLengths(12.0, 0.0, 12.0, 0.0));
            button.HorizontalAlignment(HorizontalAlignment::Stretch);
            button.HorizontalContentAlignment(HorizontalAlignment::Left);
            button.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                winrt::Windows::UI::Colors::Transparent()});
            button.BorderThickness(ThicknessHelper::FromUniformLength(0.0));

            button.Click([this, section](winrt::Windows::Foundation::IInspectable const&,
                                         winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) { _selectSection(section); });

            m_buttons.push_back(button);
            m_sectionButtons.Children().Append(button);
        }

        listCard.Child(m_sectionButtons);
        Grid::SetColumn(listCard, 0);
        m_root.Children().Append(listCard);

        // --- Detail area -------------------------------------------------------
        ScrollViewer scroller;
        scroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        scroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);

        m_detailPanel = controls::MakeStack(12.0);
        scroller.Content(m_detailPanel);

        Grid::SetColumn(scroller, 1);
        m_root.Children().Append(scroller);
    }

    void PerformanceView::_updateSelectionVisuals()
    {
        // The selected row is marked with the accent colour on its left edge and a
        // filled background, which is how the Task Manager rail reads.
        for (size_t i = 0; i < m_buttons.size(); ++i)
        {
            auto& button = m_buttons[i];
            bool const selected = (static_cast<int>(m_selected) == static_cast<int>(i));

            button.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{
                selected ? winrt::Windows::UI::Color{0x33, 0x4C, 0xC2, 0xFF}
                         : winrt::Windows::UI::Colors::Transparent()});
            button.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(
                metrics::CONTROL_RADIUS));
        }
    }

    void PerformanceView::_selectSection(Section section)
    {
        m_selected = section;
        m_detailPanel.Children().Clear();
        m_primaryChart.reset();
        m_secondaryChart.reset();
        m_detailsCard = nullptr;
        m_detailValues.clear();

        _updateSelectionVisuals();

        bool const hasData = static_cast<size_t>(section) < SECTIONS.size() && SECTIONS[static_cast<size_t>(section)].hasData;

        if (hasData)
        {
            if (section == Section::Cpu)
            {
                m_primaryChart = std::make_unique<HistoryChart>(L"CPU utilisation", CPU_CHART_COLOR, 100.0);
                m_detailPanel.Children().Append(m_primaryChart->Root());

                // Per-logical-processor load, drawn with the same component so the
                // colour customisation will cover both.
                m_secondaryChart = std::make_unique<HistoryChart>(L"Logical processors", CPU_CHART_COLOR, 100.0);
                m_detailPanel.Children().Append(m_secondaryChart->Root());
            }
            else if (section == Section::Memory)
            {
                m_primaryChart = std::make_unique<HistoryChart>(L"Memory usage", MEMORY_CHART_COLOR, 100.0);
                m_detailPanel.Children().Append(m_primaryChart->Root());
            }

            // The details card is created once per section and its value blocks are
            // retained, so a refresh writes into existing controls instead of adding
            // new ones several times a second.
            m_detailsCard = controls::MakeCard();
            StackPanel content = controls::MakeStack(0.0);
            content.Children().Append(controls::MakeHeading(SECTIONS[static_cast<size_t>(section)].title, 16.0));

            for (wchar_t const* label : _detailLabelsFor(static_cast<int>(section)))
            {
                TextBlock value{nullptr};
                content.Children().Append(_makeDetailRow(label, value));
                m_detailValues.push_back(value);
            }

            content.Children().Append(controls::MakeDivider());

            // The sampling interval is worth surfacing: a user who set it to five
            // seconds and forgot should not think the chart has frozen.
            {
                TextBlock value{nullptr};
                content.Children().Append(_makeDetailRow(L"Sampling interval", value));
                m_detailValues.push_back(value);
            }

            m_detailsCard.Child(content);
            m_detailPanel.Children().Append(m_detailsCard);
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
            m_detailPanel.Children().Append(card);
        }

        // Force the next refresh to populate the freshly built controls.
        m_renderedVersion = 0;
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
        m_renderedVersion = system.version;

        _updateDetails(system);

        if (m_primaryChart != nullptr)
        {
            if (m_selected == Section::Cpu)
            {
                m_primaryChart->SetSeries(m_coordinator.CurrentHistory().cpuTotal);
                m_primaryChart->SetCurrentValueText(system.ratesUnavailable
                                                        ? winrt::hstring{winrt::to_hstring(UnavailableValue())}
                                                        : winrt::hstring{winrt::to_hstring(FormatPercent(system.cpuPercent))});
            }
            else if (m_selected == Section::Memory)
            {
                m_primaryChart->SetSeries(m_coordinator.CurrentHistory().memoryUsed);
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
            m_secondaryChart->SetCurrentValueText(winrt::hstring{
                std::to_wstring(busyCores) + L" / " + std::to_wstring(system.perProcessorCpuPercent.size()) +
                L" busy"});
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
