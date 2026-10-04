#include "UI/WinRTUI.h"

#include "UI/Views/CpuPage.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <string>

using winrt::Microsoft::UI::Xaml::Controls::Border;
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

namespace tmpp::ui
{
    namespace
    {
        /// Minimum height of the chart grid. It grows with the window above this.
        /// Minimum height of the chart grid.
        ///
        /// The grid's rows each have a 40 pixel floor, so a 28-core machine in seven rows
        /// needs about 280. A smaller floor here would let the container squeeze the grid
        /// below what its cells can draw in, which blanks every chart.
        constexpr double CORE_GRID_MIN_HEIGHT = 300.0;

        /// Default series colour, used until the customisation feature supplies one.
        constexpr winrt::Windows::UI::Color DEFAULT_CPU_COLOR{0xFF, 0x4C, 0xC2, 0xFF};

        /// Font size of the large processor heading.
        constexpr double HEADING_FONT_SIZE = 22.0;

        /// Font size of the live clock beside it.
        constexpr double SPEED_FONT_SIZE = 15.0;

        /**
         * @brief Formats a byte count of cache as the original does.
         *
         * Task Manager shows cache in MB with one decimal (28.0 MB), while the generic
         * byte formatter would render the same value as "28.0 MB" for L2 but switch to
         * KB for L1 (1.8 MB is under the threshold). Cache is always shown in MB, so it
         * gets its own formatter rather than bending the general one.
         */
        [[nodiscard]] std::string _formatCache(uint64_t bytes)
        {
            if (bytes == 0)
            {
                return UnavailableValue();
            }

            double const megabytes = static_cast<double>(bytes) / (1024.0 * 1024.0);
            char buffer[32]{};
            std::snprintf(buffer, sizeof(buffer), "%.1f MB", megabytes);
            return buffer;
        }
    }

    CpuPage::CpuPage(core::SamplingCoordinator& coordinator, core::ChartStyle const& style)
        : m_coordinator(coordinator)
    {
        _buildLayout();

        // Applied here rather than by the caller. The page is built lazily, on first selection, so there
        // is no point after construction at which the caller reliably knows to style it -- and a chart left
        // at its constructed defaults is the whole of what the user sees until the settings are next
        // touched.
        SetAccentColor(winrt::Windows::UI::Color{0xFF, style.red, style.green, style.blue});
        SetLineWidth(style.ClampedLineWidth());
    }

    void CpuPage::_buildLayout()
    {
        m_root = Grid();
        m_root.Padding(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));

        // Rows: heading (Auto), chart caption (Auto), chart grid (star), details (Auto).
        // The chart grid is the only star row: it is the part that should absorb extra
        // height, and the details panel stays at its natural size at the bottom.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        RowDefinition gridRow;
        gridRow.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        m_root.RowDefinitions().Append(gridRow);
        m_root.RowDefinitions().Append(controls::MakeAutoRow());

        // --- Heading: the section's name, with the processor model beside it ----
        //
        // The title is the section name rather than the marketing name, matching how every other
        // performance page is headed. The model identifies the hardware, so it is kept as a secondary
        // label at the right-hand end rather than being the headline.
        Grid heading = Grid();
        heading.ColumnDefinitions().Append(controls::MakeStarColumn());
        heading.ColumnDefinitions().Append(controls::MakeAutoColumn());

        m_processorName = controls::MakeHeading(L"CPU", HEADING_FONT_SIZE);
        Grid::SetColumn(m_processorName, 0);
        heading.Children().Append(m_processorName);

        // The model is long and a narrow window would clip it, so it is trimmed from its end and
        // aligned to the bottom so it sits on the heading's baseline.
        m_processorModel = controls::MakeText(winrt::to_hstring(UnavailableValue()), SPEED_FONT_SIZE, true);
        m_processorModel.HorizontalAlignment(HorizontalAlignment::Right);
        m_processorModel.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Bottom);
        m_processorModel.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        m_processorModel.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        m_processorModel.Margin(ThicknessHelper::FromLengths(12.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_processorModel, 1);
        heading.Children().Append(m_processorModel);

        Grid::SetRow(heading, 0);
        m_root.Children().Append(heading);

        // --- Chart caption, matching the original's "% Utilization over 60 seconds" ----
        Grid captionRow = Grid();
        captionRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        captionRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
        captionRow.Margin(ThicknessHelper::FromLengths(0.0, 10.0, 0.0, 4.0));

        // Derived from the retained window rather than hard-coded, so it stays truthful
        // when the history window setting changes.
        uint32_t const windowSeconds = m_coordinator.HistorySeconds();
        std::wstring caption = L"% Utilization over ";

        // Whole minutes are expressed in minutes and everything else in seconds, with the
        // unit pluralised only when the value is not exactly one. The unpluralised form
        // produced "over 1 minutes".
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


        m_chartCaption = controls::MakeText(caption, 12.0, true);

        TextBlock maximumLabel = controls::MakeText(L"100%", 12.0, true);
        maximumLabel.HorizontalAlignment(HorizontalAlignment::Right);

        Grid::SetColumn(m_chartCaption, 0);
        Grid::SetColumn(maximumLabel, 1);
        captionRow.Children().Append(m_chartCaption);
        captionRow.Children().Append(maximumLabel);

        Grid::SetRow(captionRow, 1);
        m_root.Children().Append(captionRow);

        // --- Per-core chart grid -----------------------------------------------------
        m_coreGrid = std::make_unique<CoreGrid>();
        m_coreGrid->Root().MinHeight(CORE_GRID_MIN_HEIGHT);

        // The style is set before the cell count, because SetCoreCount is what creates the cells: a cell
        // built before the style is known takes the chart's own default and would have to be corrected
        // afterwards. CoreGrid remembers both, so the order here is belt and braces rather than the only
        // thing keeping it right.
        m_coreGrid->SetColor(DEFAULT_CPU_COLOR);
        m_coreGrid->SetCoreCount(m_coordinator.LogicalProcessorCount());

        Grid::SetRow(m_coreGrid->Root(), 2);
        m_root.Children().Append(m_coreGrid->Root());

        // --- Three-column detail panel ----------------------------------------------
        Grid details = Grid();
        details.Margin(ThicknessHelper::FromLengths(0.0, 16.0, 0.0, 0.0));

        // Three equal columns. Star rather than fixed widths so the groups stay aligned
        // as the window resizes; a wrapping panel would reflow them out of alignment.
        for (int i = 0; i < 3; ++i)
        {
            ColumnDefinition definition;
            definition.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            details.ColumnDefinitions().Append(definition);
        }

        StackPanel column1 = controls::MakeStack(4.0);
        StackPanel column2 = controls::MakeStack(4.0);
        StackPanel column3 = controls::MakeStack(4.0);

        // Each column is inset on its right edge. A right-aligned value sits flush against
        // its column boundary, so without this the value of one group runs into the label
        // of the next: "21.4%Processes" was what that produced.
        winrt::Microsoft::UI::Xaml::Thickness const columnPadding = ThicknessHelper::FromLengths(0.0, 0.0, 24.0, 0.0);
        column1.Padding(columnPadding);
        column2.Padding(columnPadding);
        column3.Padding(columnPadding);

        // Column 1: the live load, how long the machine has been up, and the two clock speeds. The
        // rated speed and the one the part is actually running at belong together, which is why both
        // sit here rather than the current speed being filed with the topology.
        m_column1.push_back(_addDetail(column1, L"Utilization"));
        m_column1.push_back(_addDetail(column1, L"Up time"));
        m_column1.push_back(_addDetail(column1, L"Base speed"));
        m_column1.push_back(_addDetail(column1, L"Speed"));

        // Column 2: what the machine is doing, counted rather than measured.
        m_column2.push_back(_addDetail(column2, L"Processes"));
        m_column2.push_back(_addDetail(column2, L"Threads"));
        m_column2.push_back(_addDetail(column2, L"Handles"));

        // Column 3: the static topology, which does not change while the process runs.
        m_column3.push_back(_addDetail(column3, L"Sockets"));
        m_column3.push_back(_addDetail(column3, L"Cores"));
        m_column3.push_back(_addDetail(column3, L"Logical processors"));
        m_column3.push_back(_addDetail(column3, L"Virtualization"));
        m_column3.push_back(_addDetail(column3, L"L1 cache"));
        m_column3.push_back(_addDetail(column3, L"L2 cache"));
        m_column3.push_back(_addDetail(column3, L"L3 cache"));
        // Every column is placed explicitly. Only the third one was, which left the other two both
        // defaulting to column 0 and drawing on top of each other: what a reader saw was two groups'
        // labels and values interleaved in the same cell.
        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 3);
        m_root.Children().Append(details);
    }

    CpuPage::DetailRow CpuPage::_addDetail(StackPanel const& column, wchar_t const* label)
    {
        // Two star columns: the label keeps its natural width on the left, the value is
        // right-aligned so a column of numbers lines up on their trailing edge.
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

    void CpuPage::SetAccentColor(winrt::Windows::UI::Color color)
    {
        if (m_coreGrid != nullptr)
        {
            m_coreGrid->SetColor(color);
        }
        if (m_totalChart != nullptr)
        {
            m_totalChart->SetLineColor(color);
        }
    }

    void CpuPage::SetLineWidth(double width)
    {
        if (m_coreGrid != nullptr)
        {
            m_coreGrid->SetLineWidth(width);
        }

        // The total-utilisation chart as well as the per-core grid. It was missed here, so the size set in
        // the settings applied to the small per-core charts and to nothing else: the large chart, which is
        // the one the setting exists to control, kept the thickness it was given at construction.
        if (m_totalChart != nullptr)
        {
            m_totalChart->SetLineWidth(width);
        }
    }

    void CpuPage::Refresh()
    {
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        // Heading: the marketing name is static, so it is written when it first becomes
        // available rather than every frame.
        if (!system.processor.modelName.empty())
        {
            m_processorModel.Text(winrt::to_hstring(system.processor.modelName));
        }

        // The current clock is not shown here. It is a figure that changes every sample and is already
        // reported in the details panel beside the rated speed, where the two can be compared; a
        // heading whose text moves several times a second is harder to read than one that does not.

        // The core grid draws one series per logical processor, which is what makes it a
        // history rather than a row of bars showing only the current instant.
        m_coreGrid->UpdateSeries(history.perProcessorCpu, history.windowSamples);

        _updateDetails(system);
    }

    void CpuPage::_updateDetails(domain::SystemView const& system)
    {
        platform::SystemProcessorInfo const& processor = system.processor;

        // Values are written into the rows created by _buildLayout. Adding rows per
        // refresh would append controls several times a second and grow without bound,
        // which is the defect this pattern exists to avoid.
        auto assign = [](std::vector<DetailRow> const& rows, size_t index, std::string const& text) {
            if (index < rows.size() && rows[index].value != nullptr)
            {
                rows[index].value.Text(winrt::to_hstring(text));
            }
        };

        // Column 1.
        assign(m_column1, 0, system.ratesUnavailable ? UnavailableValue() : FormatPercent(system.cpuPercent));
        assign(m_column1, 1, FormatDuration(system.totals.uptimeSeconds));
        assign(m_column1,
               2,
               processor.baseClockMhz > 0 ? std::to_string(processor.baseClockMhz) + " MHz" : UnavailableValue());
        assign(m_column1,
               3,
               system.processorSpeed.available
                   ? std::to_string(system.processorSpeed.currentMhz) + " MHz"
                   : UnavailableValue());

        // Column 2.
        assign(m_column2, 0, FormatCount(system.totals.processCount));
        assign(m_column2, 1, FormatCount(system.totals.threadCount));
        assign(m_column2, 2, FormatCount(system.totals.handleCount));

        // Column 3.
        assign(m_column3, 0, std::to_string(processor.socketCount));
        assign(m_column3, 1, std::to_string(processor.physicalCoreCount));
        assign(m_column3, 2, std::to_string(processor.logicalProcessorCount));
        assign(m_column3, 3, processor.virtualizationFirmwareEnabled ? "Enabled" : "Disabled");
        assign(m_column3, 4, _formatCache(processor.l1CacheBytes));
        assign(m_column3, 5, _formatCache(processor.l2CacheBytes));
        assign(m_column3, 6, _formatCache(processor.l3CacheBytes));
    }
}
