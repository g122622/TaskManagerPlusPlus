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
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // heading
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // caption
        m_root.RowDefinitions().Append(controls::MakeStarRow()); // usage chart
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // composition strip
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // details

        // --- Heading -----------------------------------------------------------
        // The original's largest text on this page is the section name, with the installed
        // capacity and type at the right-hand end of the same line. The current usage belongs in
        // its own row below, which is where the chart caption sits.
        Grid headingRow = Grid();
        headingRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        headingRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

        m_heading = controls::MakeHeading(L"Memory", HEADING_FONT_SIZE);
        Grid::SetColumn(m_heading, 0);
        headingRow.Children().Append(m_heading);

        m_installedCaption = controls::MakeText(L"", 13.0, true);
        m_installedCaption.VerticalAlignment(VerticalAlignment::Bottom);
        m_installedCaption.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_installedCaption, 1);
        headingRow.Children().Append(m_installedCaption);

        Grid::SetRow(headingRow, 0);
        m_root.Children().Append(headingRow);

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
        m_caption.VerticalAlignment(VerticalAlignment::Center);

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
        // Large charts carry a frame, matching the original. The per-core cells are framed by
        // their own container instead.
        m_chart->Root().MinHeight(CHART_MIN_HEIGHT);

        Grid::SetRow(m_chart->Root(), 2);
        m_root.Children().Append(m_chart->Root());

        // --- Composition strip -------------------------------------------------
        //
        // Windows 11 Task Manager places this directly under the chart: one proportional bar
        // divided into in-use, modified, cached and free. The legend below it repeats the labels,
        // which is how the original labels the colours without putting text inside the strip.
        StackPanel compositionBlock = controls::MakeStack(4.0);
        compositionBlock.Margin(ThicknessHelper::FromLengths(0.0, 8.0, 0.0, 0.0));

        m_composition = std::make_unique<MemoryCompositionBar>();
        compositionBlock.Children().Append(m_composition->Root());

        // A horizontal stack clips what does not fit, which cut the final legend entry off at the
        // right edge. A grid of two star columns puts two entries per row and lets a long entry
        // wrap within its own column instead of running past the edge.
        m_compositionLegend = controls::MakeGrid(2, 2, 12.0, 2.0);
        compositionBlock.Children().Append(m_compositionLegend);

        Grid::SetRow(compositionBlock, 3);
        m_root.Children().Append(compositionBlock);

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

        Grid::SetRow(details, 4);
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
        // A right-aligned value that is wider than its column would otherwise spill over the
        // next column's label, which is what produced "Non-paged poolcollected yet)".
        detail.value.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

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

        // The heading carries the installed capacity, as the original does; the current usage is
        // the caption above the chart.
        m_installedCaption.Text(winrt::to_hstring(FormatBytes(system.memory.totalPhysical)));

        m_caption.Text(winrt::to_hstring("In use " + FormatBytes(system.memoryUsedBytes) + " (" +
                                         FormatPercent(system.memoryUsedPercent) + ")  \xE2\x80\xA2  over " +
                                         std::to_string(m_coordinator.HistorySeconds()) + " s"));

        // The window rides with the data, so the chart anchors its samples to the right edge
        // instead of stretching them across the full width.
        ChartSeries series;
        series.values = history.memoryUsed;
        series.windowSamples = history.windowSamples;
        m_chart->SetSeries(series);

        _updateDetails(system);
        _updateComposition(system);
    }

    void MemoryPage::_updateComposition(domain::SystemView const& system)
    {
        platform::SystemMemoryComposition const& composition = system.memoryComposition;
        if (!composition.available)
        {
            // The probe did not read the page lists, so the strip is hidden rather than drawn
            // with four zero-length segments, which would look like a measurement of nothing.
            m_composition->Clear();
            m_compositionLegend.Children().Clear();
            m_legendValues.clear();
            return;
        }

        // The order matches the original's strip, left to right.
        //
        // The colours are deliberately distinct in both themes and are not taken from the
        // series palette: this strip encodes categories, not metrics, and reusing a series colour
        // would suggest the two are related. They are also the hook the colour-customisation
        // feature will drive (docs/ROADMAP.md, M1-6).
        struct Category
        {
            wchar_t const* label;
            winrt::Windows::UI::Color color;
            uint64_t bytes;
            bool shownInLegend;
        };

        std::vector<Category> const categories{
            {L"In use", winrt::Windows::UI::Color{0xFF, 0x4C, 0x8B, 0xF5}, composition.inUseBytes, true},
            {L"Modified", winrt::Windows::UI::Color{0xFF, 0xFF, 0xB1, 0x4A}, composition.modifiedBytes, true},
            {L"Cached", winrt::Windows::UI::Color{0xFF, 0x7A, 0x6C, 0xE8}, composition.standbyBytes, true},
            {L"Free", winrt::Windows::UI::Color{0xFF, 0x5A, 0x5A, 0x5A}, composition.freeBytes, true},
        };

        std::vector<MemoryCompositionBar::Segment> segments;
        segments.reserve(categories.size());
        for (Category const& category : categories)
        {
            segments.push_back(MemoryCompositionBar::Segment{category.label, category.color, category.bytes});
        }
        m_composition->SetSegments(segments);

        // The legend is rebuilt to match what the strip actually shows, so a category with no
        // bytes is not labelled with a value of zero beside a colour that is absent.
        m_compositionLegend.Children().Clear();
        m_legendValues.clear();

        // Two entries per row, placed explicitly. Appending without a row and column leaves every
        // entry in cell (0,0), which is what stacked all four on top of each other.
        size_t legendIndex = 0;
        for (Category const& category : categories)
        {
            if (category.bytes == 0)
            {
                continue;
            }

            StackPanel entry = controls::MakeRow(6.0);

            // A colour swatch, which is what ties the legend entry to its segment.
            winrt::Microsoft::UI::Xaml::Controls::Border swatch;
            swatch.Width(10.0);
            swatch.Height(10.0);
            swatch.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(2.0));
            swatch.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{category.color});
            entry.Children().Append(swatch);

            std::string text{winrt::to_string(winrt::hstring{category.label})};
            text += "  ";
            text += FormatBytes(category.bytes);
            text += "  (";
            text += FormatPercent(system.memory.totalPhysical > 0
                                      ? (static_cast<double>(category.bytes) * 100.0 /
                                         static_cast<double>(system.memory.totalPhysical))
                                      : 0.0);
            text += ")";

            // A smaller size than the body text so four entries fit across the width, and trimming
            // so that if one still does not fit it is elided rather than running past the edge.
            TextBlock labelBlock = controls::MakeText(winrt::to_hstring(text), 11.0, true);
            labelBlock.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            entry.Children().Append(labelBlock);

            Grid::SetRow(entry, static_cast<int32_t>(legendIndex / 2));
            Grid::SetColumn(entry, static_cast<int32_t>(legendIndex % 2));
            m_compositionLegend.Children().Append(entry);

            ++legendIndex;
        }
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
        assign(m_column2, 1, UnavailableValue());
        assign(m_column2, 2, UnavailableValue());
        assign(m_column2, 3, UnavailableValue());

        for (size_t i = 0; i < m_column3.size(); ++i)
        {
            assign(m_column3, i, UnavailableValue());
        }
    }
}
