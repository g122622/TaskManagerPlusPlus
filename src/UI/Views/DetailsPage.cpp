#include "UI/WinRTUI.h"

#include "UI/Views/DetailsPage.h"

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
using winrt::Microsoft::UI::Xaml::Visibility;

namespace tmpp::ui
{
    namespace
    {
        constexpr double HEADING_FONT_SIZE = 22.0;
        constexpr double CHART_MIN_HEIGHT = 180.0;

        /// The default process chart colour, matching the sidebar's CPU row.
        constexpr winrt::Windows::UI::Color DEFAULT_PROCESS_COLOR{0xFF, 0x4C, 0xC2, 0xFF};

        /// One process's history is not kept by the model -- the system series are, and a per-process
        /// one for every process on the machine would be thousands of rings -- so the chart shows the
        /// system's CPU trend with the process's current share stated above it.
        constexpr wchar_t const* NO_SELECTION_TEXT =
            L"Select a process in the Processes list to see its details.";

        [[nodiscard]] std::string _priorityName(int32_t basePriority)
        {
            // The Windows priority classes, by the base priority the kernel reports for the process.
            // Naming them is what the original's "Base priority" row does; the numbers alone mean
            // little to anyone who has not memorised the table.
            if (basePriority >= 24)
            {
                return "Realtime";
            }
            if (basePriority >= 13)
            {
                return "High";
            }
            if (basePriority >= 10)
            {
                return "Above normal";
            }
            if (basePriority >= 8)
            {
                return "Normal";
            }
            if (basePriority >= 6)
            {
                return "Below normal";
            }
            return "Low";
        }
    }

    DetailsPage::DetailsPage(core::SamplingCoordinator& coordinator, core::ChartStyle const& style)
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

    void DetailsPage::_buildLayout()
    {
        m_root = Grid();

        // Rows: heading (Auto), subtitle (Auto), body (star). A default-constructed RowDefinition is
        // 1* (Star), so the helpers are what stop the heading taking an equal share of the height.
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeAutoRow());
        m_root.RowDefinitions().Append(controls::MakeStarRow());

        // --- Heading -----------------------------------------------------------
        Grid headingRow = Grid();
        headingRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        headingRow.ColumnDefinitions().Append(controls::MakeAutoColumn());

        m_heading = controls::MakeHeading(L"Details", HEADING_FONT_SIZE);
        Grid::SetColumn(m_heading, 0);
        headingRow.Children().Append(m_heading);

        m_subtitle = controls::MakeText(L"", 13.0, true);
        m_subtitle.VerticalAlignment(VerticalAlignment::Bottom);
        m_subtitle.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 4.0));
        Grid::SetColumn(m_subtitle, 1);
        headingRow.Children().Append(m_subtitle);

        Grid::SetRow(headingRow, 0);
        m_root.Children().Append(headingRow);

        // --- Empty state -------------------------------------------------------
        m_emptyMessage = controls::MakeText(NO_SELECTION_TEXT, 14.0, true);
        m_emptyMessage.Margin(ThicknessHelper::FromLengths(0.0, 24.0, 0.0, 0.0));
        Grid::SetRow(m_emptyMessage, 1);
        m_root.Children().Append(m_emptyMessage);

        // --- Body --------------------------------------------------------------
        //
        // Hidden until a process is selected, so the empty state is what is seen first rather than a
        // page of dashes.
        m_body = Grid();
        m_body.Visibility(Visibility::Collapsed);
        m_body.RowDefinitions().Append(controls::MakeAutoRow()); // caption
        m_body.RowDefinitions().Append(controls::MakeStarRow()); // chart
        m_body.RowDefinitions().Append(controls::MakeAutoRow()); // details

        Grid captionRow = Grid();
        captionRow.ColumnDefinitions().Append(controls::MakeStarColumn());
        captionRow.ColumnDefinitions().Append(controls::MakeAutoColumn());
        captionRow.Margin(ThicknessHelper::FromLengths(0.0, 12.0, 0.0, 4.0));

        TextBlock caption = controls::MakeText(L"System CPU over the window", 12.0, true);
        Grid::SetColumn(caption, 0);
        captionRow.Children().Append(caption);

        TextBlock maximumLabel = controls::MakeText(L"100%", 12.0, true);
        maximumLabel.HorizontalAlignment(HorizontalAlignment::Right);
        Grid::SetColumn(maximumLabel, 1);
        captionRow.Children().Append(maximumLabel);

        Grid::SetRow(captionRow, 0);
        m_body.Children().Append(captionRow);

        // The process's own CPU history is not kept: the model holds one ring per system series, and
        // a ring per process would be thousands of them on a busy machine. The chart therefore shows
        // the system trend, which is the context in which the process's share is read.
        m_cpuChart = std::make_unique<HistoryChart>(L"", DEFAULT_PROCESS_COLOR, 100.0);
        m_cpuChart->SetHeaderVisible(false);
        m_cpuChart->Root().MinHeight(CHART_MIN_HEIGHT);

        Grid::SetRow(m_cpuChart->Root(), 1);
        m_body.Children().Append(m_cpuChart->Root());

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

        // Column 1: what the process is doing now.
        m_column1.push_back(_addDetail(column1, L"CPU"));
        m_column1.push_back(_addDetail(column1, L"Memory"));
        m_column1.push_back(_addDetail(column1, L"Disk read"));
        m_column1.push_back(_addDetail(column1, L"Disk write"));
        m_column1.push_back(_addDetail(column1, L"Page faults"));

        // Column 2: its shape.
        m_column2.push_back(_addDetail(column2, L"Threads"));
        m_column2.push_back(_addDetail(column2, L"Handles"));
        m_column2.push_back(_addDetail(column2, L"Base priority"));
        m_column2.push_back(_addDetail(column2, L"Session"));
        m_column2.push_back(_addDetail(column2, L"Parent"));

        // Column 3: cumulative totals, which the model passes through.
        m_column3.push_back(_addDetail(column3, L"Total CPU time"));
        m_column3.push_back(_addDetail(column3, L"Peak working set"));
        m_column3.push_back(_addDetail(column3, L"Virtual size"));
        m_column3.push_back(_addDetail(column3, L"I/O read"));
        m_column3.push_back(_addDetail(column3, L"I/O written"));

        Grid::SetColumn(column1, 0);
        Grid::SetColumn(column2, 1);
        Grid::SetColumn(column3, 2);
        details.Children().Append(column1);
        details.Children().Append(column2);
        details.Children().Append(column3);

        Grid::SetRow(details, 2);
        m_body.Children().Append(details);

        Grid::SetRow(m_body, 2);
        m_root.Children().Append(m_body);
    }

    DetailsPage::DetailRow DetailsPage::_addDetail(StackPanel const& column, wchar_t const* label)
    {
        Grid row = Grid();
        row.ColumnDefinitions().Append(controls::MakeAutoColumn());
        row.ColumnDefinitions().Append(controls::MakeStarColumn());

        TextBlock labelBlock = controls::MakeText(label, 13.0, true);
        labelBlock.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        Grid::SetColumn(labelBlock, 0);
        row.Children().Append(labelBlock);

        DetailRow detail;
        detail.value = controls::MakeText(winrt::to_hstring(UnavailableValue()), 13.0);
        detail.value.HorizontalAlignment(HorizontalAlignment::Right);
        detail.value.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        detail.value.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        Grid::SetColumn(detail.value, 1);
        row.Children().Append(detail.value);

        column.Children().Append(row);
        return detail;
    }

    void DetailsPage::_setDetail(std::vector<DetailRow> const& rows, size_t index, std::string const& text)
    {
        if (index < rows.size() && rows[index].value != nullptr)
        {
            rows[index].value.Text(winrt::to_hstring(text));
        }
    }

    void DetailsPage::_clearDetails()
    {
        for (size_t i = 0; i < m_column1.size(); ++i)
        {
            _setDetail(m_column1, i, UnavailableValue());
            _setDetail(m_column2, i, UnavailableValue());
            _setDetail(m_column3, i, UnavailableValue());
        }
    }

    void DetailsPage::SetPid(uint32_t pid)
    {
        if (m_pid == pid)
        {
            return;
        }

        m_pid = pid;

        // A newly selected process has not been drawn yet, so the version check must not skip it.
        m_renderedVersion = 0;
    }

    void DetailsPage::SetAccentColor(winrt::Windows::UI::Color color)
    {
        if (m_cpuChart != nullptr)
        {
            m_cpuChart->SetLineColor(color);
        }
    }

    void DetailsPage::SetLineWidth(double width)
    {
        if (m_cpuChart != nullptr)
        {
            m_cpuChart->SetLineWidth(width);
        }
    }

    void DetailsPage::Refresh()
    {
        uint64_t const version = m_coordinator.ProcessVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::ProcessSnapshotView const snapshot = m_coordinator.CurrentProcesses();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = snapshot.version;

        // Nothing selected: the empty state is what shows, rather than a page of dashes that would
        // read as a process with no measurable properties.
        if (m_pid == 0)
        {
            m_body.Visibility(Visibility::Collapsed);
            m_emptyMessage.Visibility(Visibility::Visible);
            m_heading.Text(L"Details");
            m_subtitle.Text(L"");
            return;
        }

        // The process is found by id rather than by index: the list is sorted, so an index is
        // meaningless between samples, and a process that has exited must be recognised as such.
        auto const found = std::find_if(snapshot.processes.begin(),
                                        snapshot.processes.end(),
                                        [this](domain::ProcessView const& process) {
                                            return process.identity.pid == m_pid;
                                        });

        if (found == snapshot.processes.end())
        {
            // The selected process has exited. Saying so is better than continuing to show its last
            // figures, which would look like a process that is still running.
            m_body.Visibility(Visibility::Collapsed);
            m_emptyMessage.Visibility(Visibility::Visible);
            m_heading.Text(winrt::hstring{L"Process " + std::to_wstring(m_pid)});
            m_subtitle.Text(L"has exited");
            return;
        }

        domain::ProcessView const& process = *found;

        m_body.Visibility(Visibility::Visible);
        m_emptyMessage.Visibility(Visibility::Collapsed);

        // The idle process has no image name from the platform, which is its report rather than a
        // failure; naming it is ours.
        m_heading.Text(winrt::to_hstring(ProcessDisplayName(process.identity.pid, process.imageName)));
        m_subtitle.Text(winrt::hstring{L"PID " + std::to_wstring(process.identity.pid)});

        // --- Current activity --------------------------------------------------
        _setDetail(m_column1,
                   0,
                   process.ratesUnavailable ? UnavailableValue() : FormatProcessCpuPercent(process.cpuPercent));
        _setDetail(m_column1, 1, FormatBytes(process.memory.workingSetSize));
        _setDetail(m_column1, 2, process.ratesUnavailable
                                     ? UnavailableValue()
                                     : FormatBytes(static_cast<uint64_t>(process.diskReadBytesPerSec)) + "/s");
        _setDetail(m_column1, 3, process.ratesUnavailable
                                     ? UnavailableValue()
                                     : FormatBytes(static_cast<uint64_t>(process.diskWriteBytesPerSec)) + "/s");

        char faults[48]{};
        if (process.ratesUnavailable)
        {
            _setDetail(m_column1, 4, UnavailableValue());
        }
        else
        {
            std::snprintf(faults, sizeof(faults), "%.0f/s", process.pageFaultsPerSec);
            _setDetail(m_column1, 4, std::string{faults});
        }

        // --- Shape -------------------------------------------------------------
        _setDetail(m_column2, 0, FormatCount(process.threadCount));
        _setDetail(m_column2, 1, FormatCount(process.handleCount));
        _setDetail(m_column2, 2, _priorityName(process.basePriority) + "  (" +
                                     std::to_string(process.basePriority) + ")");
        _setDetail(m_column2, 3, process.sessionId == 0 ? "Services" : std::to_string(process.sessionId));

        // The parent is named when it is in the same snapshot, and shown as a bare id when it is not,
        // which is what happens for a process whose parent has already exited.
        if (process.parentIndex != domain::NO_PARENT_INDEX &&
            process.parentIndex < snapshot.processes.size())
        {
            domain::ProcessView const& parent = snapshot.processes[process.parentIndex];
            _setDetail(m_column2,
                       4,
                       ProcessDisplayName(parent.identity.pid, parent.imageName) + "  (" +
                           std::to_string(parent.identity.pid) + ")");
        }
        else if (process.parentPid != 0)
        {
            _setDetail(m_column2, 4, std::string{"PID "} + std::to_string(process.parentPid) + "  (exited)");
        }
        else
        {
            _setDetail(m_column2, 4, UnavailableValue());
        }

        // --- Cumulative --------------------------------------------------------
        //
        // The CPU times are in 100-nanosecond units, which is what the kernel reports.
        uint64_t const totalCpuMs = (process.cpu.kernelTime + process.cpu.userTime) / 10000ull;
        _setDetail(m_column3, 0, FormatDuration(totalCpuMs / 1000ull));
        _setDetail(m_column3, 1, FormatBytes(process.memory.peakWorkingSetSize));
        _setDetail(m_column3, 2, FormatBytes(process.memory.virtualSize));
        _setDetail(m_column3, 3, FormatBytes(process.io.readTransferCount));
        _setDetail(m_column3, 4, FormatBytes(process.io.writeTransferCount));

        // --- System context ----------------------------------------------------
        ChartSeries series;
        series.values = history.cpuTotal;
        series.windowSamples = history.windowSamples;
        m_cpuChart->SetSeries(series);

        m_lastSeenVersion = snapshot.version;
    }
}
