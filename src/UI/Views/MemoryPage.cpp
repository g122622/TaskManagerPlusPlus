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
        _addModuleList();
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
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // memory modules

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

        // The legend reflows when the page is resized, which is what makes it adaptive rather than fixed
        // at whatever width the first layout pass happened to report.
        m_compositionLegend.SizeChanged(
            [this](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args) {
                _layoutLegend(args.NewSize().Width);
            });
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
        m_column3.push_back(_addDetail(column3, L"Type"));

        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 4);
        m_root.Children().Append(details);

        // --- Memory modules -----------------------------------------------------
        //
        // The host for the module list, filled once by _addModuleList. It is created here because this
        // method owns the layout, and a control used before it exists is an access violation rather than
        // a visible mistake -- which is exactly what happened when the fill was added and this was not.
        m_moduleList = controls::MakeStack(4.0);
        Grid::SetRow(m_moduleList, 5);
        m_root.Children().Append(m_moduleList);
    }

    void MemoryPage::_addModuleList()
    {
        // Stated rather than assumed: the cost of being wrong about this is a startup crash whose stack
        // names neither the control nor the line.
        if (m_moduleList == nullptr)
        {
            return;
        }
        platform::SystemMemorySlots const& slots = m_coordinator.MemorySlots();

        if (!slots.available)
        {
            // Firmware that does not describe its memory is a machine to say nothing about rather than an
            // error to report: an empty list would read as a machine with no modules.
            m_moduleList.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            return;
        }

        m_moduleList.Margin(ThicknessHelper::FromLengths(0.0, 20.0, 0.0, 0.0));

        // The header is a button so the list can be opened and closed, and the list starts collapsed: it
        // is a hardware inventory rather than a live reading, so a reader watching the charts is not
        // pushed down the page by four rows that never change.
        m_moduleChevron = winrt::Microsoft::UI::Xaml::Controls::FontIcon();
        m_moduleChevron.Glyph(L"\xE70D");  // ChevronDown, meaning closed.
        m_moduleChevron.FontSize(12.0);

        m_slotsCaption = controls::MakeHeading(
            winrt::hstring{L"Slots used: " + std::to_wstring(slots.usedSlots) + L" of " +
                          std::to_wstring(slots.totalSlots)},
            14.0);

        StackPanel toggleContent = controls::MakeStack(8.0);
        toggleContent.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
        toggleContent.VerticalAlignment(VerticalAlignment::Center);
        toggleContent.Children().Append(m_moduleChevron);
        toggleContent.Children().Append(m_slotsCaption);

        m_moduleToggle = winrt::Microsoft::UI::Xaml::Controls::Button();
        m_moduleToggle.Content(toggleContent);
        m_moduleToggle.Padding(ThicknessHelper::FromLengths(0.0, 4.0, 0.0, 4.0));
        m_moduleToggle.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        m_moduleToggle.BorderThickness(winrt::Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(0.0));
        m_moduleToggle.HorizontalAlignment(HorizontalAlignment::Left);
        m_moduleToggle.Click(
            [this](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
                bool const opening =
                    m_moduleRowsHost.Visibility() != winrt::Microsoft::UI::Xaml::Visibility::Visible;
                m_moduleRowsHost.Visibility(opening ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                                                    : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

                // The chevron states what a click will do, which is the convention every expander
                // follows.
                m_moduleChevron.Glyph(opening ? L"\xE70E" : L"\xE70D");
            });

        m_moduleList.Children().Append(m_moduleToggle);

        // The rows live in their own host so the header stays visible while they are hidden.
        m_moduleRowsHost = controls::MakeStack(2.0);
        m_moduleRowsHost.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        m_moduleList.Children().Append(m_moduleRowsHost);

        for (platform::SystemMemoryModule const& module : slots.modules)
        {
            StackPanel entry = controls::MakeStack(2.0);
            entry.Margin(ThicknessHelper::FromLengths(0.0, 6.0, 0.0, 0.0));

            ModuleRow row;

            if (!module.populated)
            {
                // An empty slot is listed so the count above makes sense, and named so the reader knows
                // which one it is.
                row.title = controls::MakeText(winrt::hstring{winrt::to_hstring(module.slot) + L"   (empty)"}, 13.0);
                entry.Children().Append(row.title);
                m_moduleRowsHost.Children().Append(entry);
                m_moduleRows.push_back(row);
                continue;
            }

            // The title is the slot and its capacity, which is what a reader looks for first.
            std::wstring title = winrt::to_hstring(module.slot).c_str();
            title += L"   ";
            title += winrt::to_hstring(FormatBytes(module.capacityBytes)).c_str();

            row.title = controls::MakeText(title, 13.0);
            entry.Children().Append(row.title);

            // The detail line carries everything else, which is what a reader compares between modules.
            //
            // The helper takes an hstring because that is what to_hstring produces and what most of the
            // pieces are; the values that are built as wstring are wrapped at their call sites.
            std::wstring detail;
            auto append = [&detail](winrt::hstring const& text) {
                if (text.empty())
                {
                    return;
                }
                if (!detail.empty())
                {
                    detail += L"  \x2022  ";
                }
                detail += text.c_str();
            };

            append(winrt::to_hstring(module.typeName));
            append(winrt::to_hstring(module.formFactorName));

            if (module.configuredSpeedMhz > 0)
            {
                std::wstring speed = std::to_wstring(module.configuredSpeedMhz) + L" MHz";

                // The rated speed is stated only when it differs, which is what an underclocked module
                // looks like. Repeating it when the two agree would be noise.
                if (module.ratedSpeedMhz > 0 && module.ratedSpeedMhz != module.configuredSpeedMhz)
                {
                    speed += L" (rated " + std::to_wstring(module.ratedSpeedMhz) + L" MHz)";
                }
                append(winrt::hstring{speed});
            }

            // 0xFFFF is the firmware's "not populated" marker for a width, not a real width.
            if (module.dataWidthBits > 0 && module.dataWidthBits != 0xFFFF)
            {
                append(winrt::hstring{std::to_wstring(module.dataWidthBits) + L"-bit"});
            }

            if (module.voltageMillivolts > 0)
            {
                wchar_t volts[32]{};
                // Volts with two decimals is how modules are described; the firmware reports millivolts.
                swprintf_s(volts, L"%.2f V", static_cast<double>(module.voltageMillivolts) / 1000.0);
                append(winrt::hstring{volts});
            }

            if (!module.manufacturer.empty())
            {
                append(winrt::to_hstring(module.manufacturer));
            }

            if (!module.partNumber.empty())
            {
                append(winrt::to_hstring(module.partNumber));
            }

            row.detail = controls::MakeText(detail, 12.0, true);
            row.detail.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            entry.Children().Append(row.detail);

            m_moduleRowsHost.Children().Append(entry);
            m_moduleRows.push_back(row);
        }
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

    void MemoryPage::SetLineWidth(double width)
    {
        if (m_chart != nullptr)
        {
            m_chart->SetLineWidth(width);
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
            m_legendEntries.clear();
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

        // Hardware reserved is the difference between what the modules provide and what the operating
        // system can use: the firmware, the integrated graphics and any mapped devices take their share
        // before Windows starts. It is drawn first, on the far left, because it is the part of the
        // installed memory that is not available to anything else.
        uint64_t moduleTotalBytes = 0;
        for (platform::SystemMemoryModule const& module : m_coordinator.MemorySlots().modules)
        {
            moduleTotalBytes += module.capacityBytes;
        }

        uint64_t const hardwareReserved =
            (moduleTotalBytes > system.memory.totalPhysical) ? (moduleTotalBytes - system.memory.totalPhysical) : 0;

        std::vector<Category> const categories{
            {L"Hardware reserved", winrt::Windows::UI::Color{0xFF, 0xE8, 0x4A, 0x4A}, hardwareReserved, true},
            {L"In use", winrt::Windows::UI::Color{0xFF, 0x4C, 0x8B, 0xF5}, composition.inUseBytes, true},
            {L"Modified", winrt::Windows::UI::Color{0xFF, 0xFF, 0xB1, 0x4A}, composition.modifiedBytes, true},
            {L"Cached", winrt::Windows::UI::Color{0xFF, 0x7A, 0x6C, 0xE8}, composition.standbyBytes, true},
            {L"Free", winrt::Windows::UI::Color{0xFF, 0x5A, 0x5A, 0x5A}, composition.freeBytes, true},
        };

        // The percentages are of the installed memory rather than of what the operating system can use,
        // so the segments account for the whole of it: a reserved sliver that was divided by the usable
        // total would make the parts exceed the whole.
        uint64_t const installedBytes =
            (moduleTotalBytes > 0) ? moduleTotalBytes : system.memory.totalPhysical;

        std::vector<MemoryCompositionBar::Segment> segments;
        segments.reserve(categories.size());
        for (Category const& category : categories)
        {
            segments.push_back(MemoryCompositionBar::Segment{category.label, category.color, category.bytes});
        }
        m_composition->SetSegments(segments);

        // The legend is rebuilt to match what the strip shows, so a category with no bytes is not
        // labelled with a value of zero beside a colour that is absent.
        //
        // The remembered entries are cleared with the panel's children. Leaving them behind was a real
        // defect: the vector grew by one entry per category on every sample while the panel only ever
        // held the current ones, and the layout sizes the grid from the vector, so the legend grew a row
        // taller every second and pushed the chart above it up the page.
        m_compositionLegend.Children().Clear();
        m_legendEntries.clear();
        m_legendValues.clear();
        // Where each entry sits is decided by _layoutLegend once the panel's width is known; this loop
        // only builds them.
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
            text += FormatPercent(installedBytes > 0
                                      ? (static_cast<double>(category.bytes) * 100.0 /
                                         static_cast<double>(installedBytes))
                                      : 0.0);
            text += ")";

            // A smaller size than the body text so several entries fit across the width, and trimming
            // so that if one still does not fit it is elided rather than running past the edge.
            TextBlock labelBlock = controls::MakeText(winrt::to_hstring(text), 11.0, true);
            labelBlock.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            entry.Children().Append(labelBlock);

            // The entry has to be added to the panel as well as remembered: the layout pass below
            // assigns its cell, but a panel does not draw what was never appended to it, which is what
            // made the whole legend disappear.
            m_compositionLegend.Children().Append(entry);
            m_legendEntries.push_back(entry);
        }

        // The entries were built; where they sit is decided by the width, which is not known until the
        // panel has been laid out. Laying them out here as well handles the case where it already has
        // been, which is what a later refresh sees.
        _layoutLegend(m_compositionLegend.ActualWidth());
    }

    void MemoryPage::_layoutLegend(double availableWidth)
    {
        if (m_compositionLegend == nullptr || m_legendEntries.empty())
        {
            return;
        }

        // An entry's width is measured rather than assumed: the labels carry different amounts of text,
        // and a fixed column count is what put two per row on a wide page.
        //
        // Before the first layout pass the width is zero, in which case the entries are left in a single
        // column until the size-changed handler reports a real width.
        constexpr double MIN_ENTRY_WIDTH = 190.0;
        constexpr double COLUMN_SPACING = 16.0;

        size_t columns = 1;
        if (availableWidth > 0.0)
        {
            columns = static_cast<size_t>((availableWidth + COLUMN_SPACING) / (MIN_ENTRY_WIDTH + COLUMN_SPACING));
            columns = (std::max)(columns, static_cast<size_t>(1));
            // More columns than entries would leave the grid wider than its content and the entries
            // spread across empty cells.
            columns = (std::min)(columns, m_legendEntries.size());
        }

        // The entry set is what the layout is derived from, so it is captured before the definitions are
        // touched.
        size_t const entryCount = m_legendEntries.size();
        size_t const entriesPerRow = columns;
        size_t const wantedRows = (entryCount + entriesPerRow - 1) / entriesPerRow;

        // The entries are rebuilt on every sample, so their cells must be assigned on every pass: a new
        // element defaults to cell (0,0), and skipping the placement because the counts already match is
        // what stacked all of them on top of each other.
        for (size_t index = 0; index < m_legendEntries.size(); ++index)
        {
            Grid::SetRow(m_legendEntries[index], static_cast<int32_t>(index / columns));
            Grid::SetColumn(m_legendEntries[index], static_cast<int32_t>(index % columns));
        }

        // Only the definitions are left alone when they already fit. Rebuilding them changes the panel's
        // height, which raises SizeChanged and calls back into here; the column and row counts are all
        // that decide the layout, so a pass that would produce the same pair has nothing to change.
        if (m_compositionLegend.ColumnDefinitions().Size() == columns &&
            m_compositionLegend.RowDefinitions().Size() == wantedRows)
        {
            m_legendColumns = columns;
            return;
        }

        m_compositionLegend.ColumnDefinitions().Clear();
        m_compositionLegend.RowDefinitions().Clear();

        for (size_t column = 0; column < columns; ++column)
        {
            // Equal columns. The gap between them is the grid's own column spacing, set where the panel
            // was built, rather than a margin on each column.
            m_compositionLegend.ColumnDefinitions().Append(controls::MakeStarColumn());
        }

        for (size_t row = 0; row < wantedRows; ++row)
        {
            m_compositionLegend.RowDefinitions().Append(controls::MakeAutoRow());
        }

        m_legendColumns = columns;
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

        // The kernel's own accounting, from GetPerformanceInfo. It arrives with the memory read, so a
        // machine whose version of that call fails keeps the dashes rather than showing zeroes.
        if (system.memory.kernelAccountingAvailable)
        {
            assign(m_column2, 1, FormatBytes(system.memory.systemCacheBytes));
            assign(m_column2, 2, FormatBytes(system.memory.pagedPoolBytes));
            assign(m_column2, 3, FormatBytes(system.memory.nonPagedPoolBytes));
        }
        else
        {
            assign(m_column2, 1, UnavailableValue());
            assign(m_column2, 2, UnavailableValue());
            assign(m_column2, 3, UnavailableValue());
        }

        // Column 3 describes what is installed, which the firmware table answers. A machine without a
        // readable table keeps the dashes, since a zero would read as a measurement that was taken.
        platform::SystemMemorySlots const& slots = m_coordinator.MemorySlots();
        if (slots.available && slots.usedSlots > 0)
        {
            // The rows state a single figure each, and a machine with mixed modules has no one answer.
            // The first populated module is reported, and a star marks a column whose modules differ;
            // the per-module list below carries what each one actually is.
            platform::SystemMemoryModule const* first = nullptr;
            bool mixedSpeed = false;
            bool mixedType = false;
            bool mixedForm = false;

            for (platform::SystemMemoryModule const& module : slots.modules)
            {
                if (!module.populated)
                {
                    continue;
                }

                if (first == nullptr)
                {
                    first = &module;
                    continue;
                }

                mixedSpeed = mixedSpeed || (module.configuredSpeedMhz != first->configuredSpeedMhz);
                mixedType = mixedType || (module.typeName != first->typeName);
                mixedForm = mixedForm || (module.formFactorName != first->formFactorName);
            }

            if (first != nullptr)
            {
                auto const described = [](std::string const& name) {
                    return name.empty() || name == "Unknown" ? std::string{} : name;
                };

                std::string speed;
                if (first->configuredSpeedMhz > 0)
                {
                    speed = std::to_string(first->configuredSpeedMhz) + " MHz";
                    if (mixedSpeed)
                    {
                        speed += " *";
                    }
                }

                std::string const form = described(first->formFactorName);
                std::string const type = described(first->typeName);

                assign(m_column3, 0, speed.empty() ? UnavailableValue() : speed);
                assign(m_column3, 2, form.empty() ? UnavailableValue() : form + (mixedForm ? " *" : ""));
                assign(m_column3, 3, type.empty() ? UnavailableValue() : type + (mixedType ? " *" : ""));
            }

            assign(m_column3, 1, std::to_string(slots.usedSlots) + " of " + std::to_string(slots.totalSlots));
        }
        else
        {
            for (size_t i = 0; i < m_column3.size(); ++i)
            {
                assign(m_column3, i, UnavailableValue());
            }
        }
    }
}
