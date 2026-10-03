#include "UI/WinRTUI.h"

#include "UI/ProcessesView.h"

#include "UI/Controls.h"
#include "UI/Formatting.h"
#include "UI/Theme.h"

#include <array>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;
using winrt::Microsoft::UI::Xaml::Visibility;

namespace tmpp::ui
{
    namespace
    {
        /**
         * @brief Column definition for the process list.
         *
         * Declared as data so the header, the row builder and the sort mapping all
         * read from one table. Adding a column then cannot leave the header and the
         * row builder disagreeing about order.
         */
        struct ColumnSpec
        {
            wchar_t const* title;
            double width;
            bool rightAligned;
            SortColumn sortColumn;
        };

        /// The columns shown by default, matching the Windows 11 Task Manager
        /// process page. GPU and Network are omitted until those probes exist
        /// (see docs/ROADMAP.md); an empty column would be worse than none.
        constexpr std::array<ColumnSpec, 6> COLUMNS{{
            {L"Name", 300.0, false, SortColumn::Name},
            {L"PID", 80.0, true, SortColumn::Pid},
            {L"Status", 100.0, false, SortColumn::Status},
            {L"CPU", 90.0, true, SortColumn::Cpu},
            {L"Memory", 120.0, true, SortColumn::Memory},
            {L"Threads", 90.0, true, SortColumn::Threads},
        }};

        /// Diameter of the per-row status dot.
        constexpr double STATUS_DOT_SIZE = 8.0;

        /**
         * @brief Chooses a status colour for a process.
         *
         * Colour is used sparingly: a muted dot when the rate is not yet known, so a
         * first-sample list does not look as though everything is broken, and warm
         * only when a process is genuinely loading the machine.
         */
        [[nodiscard]] winrt::Windows::UI::Color _statusColor(domain::ProcessView const& process)
        {
            if (process.ratesUnavailable)
            {
                return winrt::Windows::UI::Colors::Gray();
            }
            if (process.cpuPercent >= 50.0)
            {
                return winrt::Windows::UI::Colors::OrangeRed();
            }
            return winrt::Windows::UI::Colors::MediumSeaGreen();
        }

        /// Sum of the column widths, so the row host can size its canvas without
        /// measuring anything.
        [[nodiscard]] constexpr double _totalRowWidth() noexcept
        {
            double width = 0.0;
            for (ColumnSpec const& column : COLUMNS)
            {
                width += column.width;
            }
            return width;
        }
    }

    ProcessesView::ProcessesView(core::SamplingCoordinator& coordinator)
        : m_coordinator(coordinator), m_listModel(std::make_unique<ProcessListModel>())
    {
        _buildLayout();
    }

    void ProcessesView::_buildLayout()
    {
        m_root = Grid();
        m_root.Padding(ThicknessHelper::FromLengths(metrics::PAGE_MARGIN, 12.0, metrics::PAGE_MARGIN, 8.0));
        m_root.RowDefinitions().Append(RowDefinition{}); // toolbar
        m_root.RowDefinitions().Append(RowDefinition{}); // column headers
        m_root.RowDefinitions().Append(RowDefinition{}); // rows

        // --- Toolbar -----------------------------------------------------------
        StackPanel toolbar;
        toolbar.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
        toolbar.Spacing(12.0);
        toolbar.VerticalAlignment(VerticalAlignment::Center);
        toolbar.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 8.0));

        m_search = controls::MakeSearchBox(L"Search processes");
        m_search.TextChanged(
            [this](winrt::Windows::Foundation::IInspectable const& sender, TextChangedEventArgs const&) {
                auto const box = sender.try_as<winrt::Microsoft::UI::Xaml::Controls::TextBox>();
                if (box == nullptr)
                {
                    return;
                }
                // The filter is applied on the next refresh rather than here, so a fast
                // typist does not trigger a rebuild per keystroke.
                m_query.filter = winrt::to_string(box.Text());
                m_renderedVersion = 0;
            });
        toolbar.Children().Append(m_search);

        m_summary = controls::MakeText(L"", 13.0, true);
        m_summary.VerticalAlignment(VerticalAlignment::Center);
        toolbar.Children().Append(m_summary);

        Grid::SetRow(toolbar, 0);
        m_root.Children().Append(toolbar);

        // --- Column headers ----------------------------------------------------
        Grid header = Grid();
        header.Padding(ThicknessHelper::FromLengths(12.0, 6.0, 12.0, 6.0));
        header.Background(controls::ThemedBrush(theme::LAYER_BACKGROUND));
        for (ColumnSpec const& column : COLUMNS)
        {
            ColumnDefinition definition;
            definition.Width(GridLengthHelper::FromPixels(column.width));
            header.ColumnDefinitions().Append(definition);
        }

        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            ColumnSpec const& column = COLUMNS[i];

            // A header is a clickable sort target, matching every list view.
            winrt::Microsoft::UI::Xaml::Controls::Button button;
            button.Content(winrt::box_value(winrt::hstring{column.title}));
            button.Padding(ThicknessHelper::FromLengths(8.0, 2.0, 8.0, 2.0));
            button.HorizontalAlignment(column.rightAligned ? HorizontalAlignment::Right : HorizontalAlignment::Left);
            button.HorizontalContentAlignment(column.rightAligned ? HorizontalAlignment::Right
                                                                 : HorizontalAlignment::Left);

            SortColumn const sortColumn = column.sortColumn;
            button.Click([this, sortColumn](winrt::Windows::Foundation::IInspectable const&,
                                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
                if (m_query.column == sortColumn)
                {
                    m_query.direction = (m_query.direction == SortDirection::Ascending) ? SortDirection::Descending
                                                                                       : SortDirection::Ascending;
                }
                else
                {
                    m_query.column = sortColumn;
                    // Numeric columns are most useful largest-first; a name column is
                    // most useful A-Z.
                    m_query.direction = (sortColumn == SortColumn::Name) ? SortDirection::Ascending
                                                                        : SortDirection::Descending;
                }
                m_renderedVersion = 0;
            });

            Grid::SetColumn(button, static_cast<int32_t>(i));
            header.Children().Append(button);
        }

        Grid::SetRow(header, 1);
        m_root.Children().Append(header);

        // --- Rows --------------------------------------------------------------
        Grid listArea = Grid();

        m_rowHost = std::make_unique<RowHost>(metrics::PROCESS_ROW_HEIGHT, _totalRowWidth());
        listArea.Children().Append(m_rowHost->Root());

        m_emptyMessage = controls::MakeText(L"No processes match the filter.", 14.0, true);
        m_emptyMessage.HorizontalAlignment(HorizontalAlignment::Center);
        m_emptyMessage.VerticalAlignment(VerticalAlignment::Center);
        m_emptyMessage.Visibility(Visibility::Collapsed);
        listArea.Children().Append(m_emptyMessage);

        Grid::SetRow(listArea, 2);
        m_root.Children().Append(listArea);
    }

    winrt::Microsoft::UI::Xaml::FrameworkElement ProcessesView::_createRow()
    {
        Grid row = Grid();
        row.Padding(ThicknessHelper::FromLengths(12.0, 0.0, 12.0, 0.0));
        row.VerticalAlignment(VerticalAlignment::Center);

        for (ColumnSpec const& column : COLUMNS)
        {
            ColumnDefinition definition;
            definition.Width(GridLengthHelper::FromPixels(column.width));
            row.ColumnDefinitions().Append(definition);
        }

        // Name column: a status dot plus the image name.
        StackPanel nameCell;
        nameCell.Orientation(winrt::Microsoft::UI::Xaml::Controls::Orientation::Horizontal);
        nameCell.Spacing(6.0);
        nameCell.VerticalAlignment(VerticalAlignment::Center);

        winrt::Microsoft::UI::Xaml::Controls::Border dot;
        dot.Width(STATUS_DOT_SIZE);
        dot.Height(STATUS_DOT_SIZE);
        dot.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(STATUS_DOT_SIZE / 2.0));
        dot.VerticalAlignment(VerticalAlignment::Center);
        nameCell.Children().Append(dot);
        nameCell.Children().Append(controls::MakeText(L"", 13.0));

        Grid::SetColumn(nameCell, 0);
        row.Children().Append(nameCell);

        // The remaining columns are plain text.
        for (size_t i = 1; i < COLUMNS.size(); ++i)
        {
            TextBlock cell = controls::MakeText(L"", 13.0);
            cell.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            cell.VerticalAlignment(VerticalAlignment::Center);
            cell.HorizontalAlignment(COLUMNS[i].rightAligned ? HorizontalAlignment::Right : HorizontalAlignment::Left);
            Grid::SetColumn(cell, static_cast<int32_t>(i));
            row.Children().Append(cell);
        }

        return row;
    }

    void ProcessesView::_bindRow(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, uint32_t rowIndex)
    {
        auto const row = element.try_as<Grid>();
        if (row == nullptr)
        {
            return;
        }

        auto const& visible = m_listModel->VisibleIndices();
        if (rowIndex >= visible.size())
        {
            return;
        }

        uint32_t const processIndex = visible[rowIndex];
        if (processIndex >= m_snapshot.processes.size())
        {
            return;
        }

        domain::ProcessView const& process = m_snapshot.processes[processIndex];

        // Column 0: dot and name. The idle process has no image name from the
        // platform, which is its report rather than a failure; naming it is ours.
        if (auto const nameCell = row.Children().GetAt(0).try_as<StackPanel>())
        {
            if (auto const dot = nameCell.Children().GetAt(0).try_as<winrt::Microsoft::UI::Xaml::Controls::Border>())
            {
                using winrt::Microsoft::UI::Xaml::Media::SolidColorBrush;
                dot.Background(SolidColorBrush(_statusColor(process)));
            }
            if (auto const label = nameCell.Children().GetAt(1).try_as<TextBlock>())
            {
                label.Text(winrt::to_hstring(ProcessDisplayName(process.identity.pid, process.imageName)));
            }
        }

        // A value that could not be derived shows an em dash, not a zero: a zero would
        // claim the process is idle when the truth is that the counters are not yet
        // available.
        auto setCell = [&row](int32_t column, std::string const& text) {
            if (auto const cell = row.Children().GetAt(column).try_as<TextBlock>())
            {
                cell.Text(winrt::to_hstring(text));
            }
        };

        setCell(1, std::to_string(process.identity.pid));
        setCell(2, process.ratesUnavailable ? UnavailableValue() : std::string{"Running"});
        setCell(3, process.ratesUnavailable ? UnavailableValue() : FormatProcessCpuPercent(process.cpuPercent));
        setCell(4, FormatBytes(process.memory.workingSetSize));
        setCell(5, FormatCount(process.threadCount));
    }

    void ProcessesView::_updateSummary()
    {
        if (m_listModel->IsFiltered())
        {
            m_summary.Text(winrt::hstring{L"Showing " + std::to_wstring(m_listModel->VisibleCount()) + L" of " +
                                          std::to_wstring(m_listModel->TotalCount()) + L" processes"});
        }
        else
        {
            m_summary.Text(winrt::hstring{std::to_wstring(m_listModel->TotalCount()) + L" processes"});
        }
    }

    void ProcessesView::Refresh()
    {
        // Poll the version first: comparing a number is effectively free, whereas
        // CurrentProcesses() is a deep copy of every process -- hundreds of strings on
        // a busy system. The UI polls far more often than the sampler publishes, so
        // without this check the render loop would copy the whole snapshot ten times a
        // second for nothing.
        uint64_t const version = m_coordinator.ProcessVersion();
        bool const forced = (m_renderedVersion == 0);
        if (version == m_renderedVersion && !forced)
        {
            return;
        }

        ListQuery const previousQuery = m_listModel->Query();

        m_snapshot = m_coordinator.CurrentProcesses();
        m_renderedVersion = version;

        m_listModel->Update(m_snapshot, m_query);
        _updateSummary();

        bool const empty = m_listModel->VisibleCount() == 0;
        m_emptyMessage.Visibility(empty ? Visibility::Visible : Visibility::Collapsed);
        m_rowHost->Root().Visibility(empty ? Visibility::Collapsed : Visibility::Visible);

        uint32_t const rowCount = static_cast<uint32_t>(m_listModel->VisibleCount());

        // Rebuilding only when the row count or the ordering changed keeps a normal
        // sample from disturbing the scroll position.
        bool const orderingChanged = forced || previousQuery.column != m_query.column ||
                                     previousQuery.direction != m_query.direction ||
                                     previousQuery.filter != m_query.filter ||
                                     rowCount != static_cast<uint32_t>(m_rowHost->LiveRowCount());

        if (orderingChanged)
        {
            m_rowHost->SetRowCount(rowCount,
                                   [this] { return _createRow(); },
                                   [this](winrt::Microsoft::UI::Xaml::FrameworkElement const& element,
                                          uint32_t index) { _bindRow(element, index); });
        }
        else
        {
            m_rowHost->RefreshVisibleRows();
        }
    }
}
