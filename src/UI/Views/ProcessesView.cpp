#include "UI/WinRTUI.h"

#include "UI/Views/ProcessesView.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <array>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::Controls::TextChangedEventArgs;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
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

        /// The columns shown, in the order Windows 11 Task Manager shows them.
        ///
        /// Name, PID, CPU, Memory, Disk, Network and GPU. There is deliberately no status column: the
        /// original has none, and a per-process "Running" that is never anything else is a column of
        /// identical text.
        ///
        /// The widths are proportions of the original's layout rather than its pixel values, since this
        /// list sizes its columns in effective pixels and the original's are drawn at a different scale.
        constexpr std::array<ColumnSpec, 7> COLUMNS{{
            {L"Name", 300.0, false, SortColumn::Name},
            {L"PID", 80.0, true, SortColumn::Pid},
            {L"CPU", 90.0, true, SortColumn::Cpu},
            {L"Memory", 120.0, true, SortColumn::Memory},
            {L"Disk", 100.0, true, SortColumn::Disk},
            {L"Network", 100.0, true, SortColumn::Network},
            {L"GPU", 80.0, true, SortColumn::Gpu},
        }};

        /// Diameter of the per-row status dot.
        constexpr double STATUS_DOT_SIZE = 8.0;

        /// Height of the band carrying the system-wide aggregates.
        constexpr double AGGREGATE_ROW_HEIGHT = 44.0;

        /// Height of the band carrying the column names.
        constexpr double LABEL_ROW_HEIGHT = 28.0;

        /// Font size of an aggregate figure. Much larger than the rows below it, which is what makes the
        /// header readable at a glance rather than a second row of the same text.
        constexpr double AGGREGATE_FONT_SIZE = 20.0;

        /// Fill for the selected process row. The Details page shows one process, so the
        /// list has to say which one is being shown.
        constexpr winrt::Windows::UI::Color ROW_SELECTION_FILL{0x33, 0x4C, 0xC2, 0xFF};

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

        /**
         * @brief Formats a per-process throughput, or a zero when nothing moved.
         *
         * Unlike a rate that could not be derived, a rate of zero is a real measurement here: it says the
         * process moved no bytes over the interval, which is what most processes do most of the time. A
         * dash would be wrong, and it is what the original shows.
         */
        [[nodiscard]] std::string _formatThroughput(double bytesPerSecond)
        {
            if (bytesPerSecond < 0.5)
            {
                return "0 MB/s";
            }
            return FormatBytes(static_cast<uint64_t>(bytesPerSecond)) + "/s";
        }

        /**
         * @brief The busiest adapter's utilisation, as a percentage.
         *
         * The busiest rather than the sum: a machine with two adapters each at 40 percent is not using 80
         * percent of any link, and the sum could exceed 100 on a machine with several.
         */
        [[nodiscard]] double _peakNetworkUtilization(std::vector<domain::NetworkActivity> const& networks)
        {
            double peak = 0.0;
            for (domain::NetworkActivity const& network : networks)
            {
                peak = (std::max)(peak, network.UtilizationPercent());
            }
            return peak;
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

        // Two fixed rows (toolbar, column headers) and a star row for the list. The
        // star is what gives the row host a definite height to fill; with an Auto row
        // the list would size to its content and the virtualisation window would be
        // wrong.
        // The toolbar and headers size to their content; the list takes the remainder.
        // Content-sized rows must be named explicitly, because GridLength defaults to 1*
        // and a bare definition would claim an equal third of the height instead.
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // toolbar
        m_root.RowDefinitions().Append(controls::MakeAutoRow()); // column headers
        m_root.RowDefinitions().Append(controls::MakeStarRow()); // rows

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

        // An action's outcome follows the process count on the same line, so it appears where the user
        // is already looking after invoking a menu item.
        //
        // Constructed here rather than further down: it is appended to the toolbar on the next line,
        // and appending a null element is an access violation rather than a visible mistake. The
        // earlier version created it a few dozen lines later and crashed the application at startup.
        m_actionMessage = controls::MakeText(L"", 13.0, true);
        m_actionMessage.VerticalAlignment(VerticalAlignment::Center);
        m_actionMessage.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        m_actionMessage.Margin(ThicknessHelper::FromLengths(16.0, 0.0, 0.0, 0.0));
        toolbar.Children().Append(m_actionMessage);

        Grid::SetRow(toolbar, 0);
        m_root.Children().Append(toolbar);

        // --- Column headers ----------------------------------------------------
        //
        // Two bands, as the original has: the system-wide aggregate above each metric column, then the
        // column's name below it. The aggregates are what makes the header useful at a glance -- the
        // question "is the machine busy?" is answered without reading any row.
        Grid header = Grid();
        header.Background(controls::ThemedBrush(theme::LAYER_BACKGROUND));

        // The aggregate band is taller than the label band, because its figures are several times the
        // size of the labels beneath them.
        header.RowDefinitions().Append(controls::MakeFixedRow(AGGREGATE_ROW_HEIGHT));
        header.RowDefinitions().Append(controls::MakeFixedRow(LABEL_ROW_HEIGHT));

        for (ColumnSpec const& column : COLUMNS)
        {
            ColumnDefinition definition;
            definition.Width(GridLengthHelper::FromPixels(column.width));
            header.ColumnDefinitions().Append(definition);
        }

        // One aggregate block per column, right-aligned over the column it summarises. Name and PID have
        // no aggregate: there is no machine-wide name or process id to report.
        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            ColumnSpec const& column = COLUMNS[i];
            if (column.sortColumn == SortColumn::Name || column.sortColumn == SortColumn::Pid)
            {
                continue;
            }

            TextBlock aggregate = controls::MakeText(L"", AGGREGATE_FONT_SIZE);
            aggregate.HorizontalAlignment(HorizontalAlignment::Right);
            aggregate.VerticalAlignment(VerticalAlignment::Center);
            aggregate.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            aggregate.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 12.0, 0.0));

            Grid::SetColumn(aggregate, static_cast<int32_t>(i));
            Grid::SetRow(aggregate, 0);
            header.Children().Append(aggregate);

            // Kept in column order so _updateHeaderAggregates can write them by column rather than by
            // the order they happened to be created in.
            m_aggregates.push_back({column.sortColumn, aggregate});
        }

        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            ColumnSpec const& column = COLUMNS[i];

            // A header is a clickable sort target, matching every list view.
            winrt::Microsoft::UI::Xaml::Controls::Button button;
            button.Content(winrt::box_value(winrt::hstring{column.title}));
            button.Padding(ThicknessHelper::FromLengths(8.0, 0.0, 8.0, 0.0));
            button.Background(
                winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            button.BorderThickness(ThicknessHelper::FromUniformLength(0.0));
            button.VerticalAlignment(VerticalAlignment::Stretch);
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
            Grid::SetRow(button, 1);
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

        // A transparent background rather than none: a null Background is not hit-testable in WinUI,
        // so a row without one would never receive the pointer and could not be selected. The same
        // brush doubles as the selection fill, which is set in _bindRow.
        row.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));

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

        // The selection fill, and the click that sets it. The process id is captured by value
        // because the row is recycled for a different process when the list is resorted.
        row.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            process.identity.pid == m_selectedPid ? ROW_SELECTION_FILL : winrt::Windows::UI::Colors::Transparent()));
        uint32_t const pid = process.identity.pid;
        row.Tapped([this, pid](winrt::Windows::Foundation::IInspectable const&,
                               winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&) {
            _selectRow(pid);
        });

        // The context menu is rebuilt on every binding rather than attached once, because a row is
        // recycled for a different process when the list is reordered and the menu has to name the
        // right one.
        std::string const displayName = ProcessDisplayName(process.identity.pid, process.imageName);

        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout menu;

        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem endTask;
        endTask.Text(L"End task");
        endTask.Click([this, pid, displayName](winrt::Windows::Foundation::IInspectable const&,
                                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
            _confirmAndTerminate(pid, winrt::to_hstring(displayName), /*entireTree=*/false);
        });
        menu.Items().Append(endTask);

        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem endTree;
        endTree.Text(L"End process tree");
        endTree.Click([this, pid, displayName](winrt::Windows::Foundation::IInspectable const&,
                                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {
            _confirmAndTerminate(pid, winrt::to_hstring(displayName), /*entireTree=*/true);
        });
        menu.Items().Append(endTree);

        row.ContextFlyout(menu);

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
        setCell(2, process.ratesUnavailable ? UnavailableValue() : FormatProcessCpuPercent(process.cpuPercent));
        setCell(3, FormatBytes(process.memory.workingSetSize));

        // Disk is the sum of the two directions: one figure per process is what the column has room for,
        // and a process's total I/O is what the row is read for. Reads and writes are separated on the
        // device's own page, where there is space to show both.
        setCell(4, _formatThroughput(process.diskReadBytesPerSec + process.diskWriteBytesPerSec));

        // Network is per-process and not yet collected, so it states that rather than showing a zero:
        // a zero would claim the process is using none, which is a different claim from not knowing.
        setCell(5, UnavailableValue());

        // GPU comes from the same performance counters as the GPU page, which do publish a per-process
        // instance for every engine.
        setCell(6, process.gpuPercent > 0.0 ? FormatProcessCpuPercent(process.gpuPercent) : std::string{"0%"});
    }

    void ProcessesView::_selectRow(uint32_t pid)
    {
        if (m_selectedPid == pid)
        {
            return;
        }

        m_selectedPid = pid;

        // Repainted in place rather than by rebuilding: the row count and the ordering have not
        // changed, and rebuilding would discard the scroll position.
        m_rowHost->RefreshVisibleRows();

        if (m_onSelectionChanged)
        {
            m_onSelectionChanged(pid);
        }
    }

    void ProcessesView::SetSelectionHandler(std::function<void(uint32_t)> handler)
    {
        m_onSelectionChanged = std::move(handler);
    }

    void ProcessesView::SetTerminateHandler(std::function<platform::ProcessActionResult(uint32_t, bool)> handler)
    {
        m_onTerminate = std::move(handler);
    }

    void ProcessesView::_confirmAndTerminate(uint32_t pid, winrt::hstring const& name, bool entireTree)
    {
        if (!m_onTerminate)
        {
            // Nothing is wired up to act. Saying so is better than a menu item that silently does
            // nothing.
            ReportActionOutcome(platform::ProcessActionResult{platform::ProcessActionOutcome::Failed,
                                                               0,
                                                               0,
                                                               "No handler is available to end processes."},
                                name);
            return;
        }

        // Confirmation before ending anything. TerminateProcess gives the target no chance to save
        // work, and a task manager is a list of names a user can misread; the original asks too.
        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(m_root.XamlRoot());
        dialog.Title(winrt::box_value(winrt::hstring{entireTree ? L"End process tree?" : L"End task?"}));

        winrt::hstring const body =
            entireTree
                ? winrt::hstring{L"This will end "} + name + L" and every process it started."
                : winrt::hstring{L"This will end "} + name + L". Unsaved work will be lost.";
        dialog.Content(winrt::box_value(body));
        dialog.PrimaryButtonText(L"End");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);

        // The dialog is shown without awaiting it. Awaiting from a UI event handler would need a
        // coroutine, and the outcome is handled in the continuation either way, so the callback form
        // expresses the same thing without turning the handler into a coroutine.
        auto const pidCopy = pid;
        auto const treeCopy = entireTree;
        auto const nameCopy = name;

        dialog.PrimaryButtonClick(
            [this, pidCopy, treeCopy, nameCopy](
                winrt::Microsoft::UI::Xaml::Controls::ContentDialog const&,
                winrt::Microsoft::UI::Xaml::Controls::ContentDialogButtonClickEventArgs const&) {
                if (!m_onTerminate)
                {
                    return;
                }

                platform::ProcessActionResult const result = m_onTerminate(pidCopy, treeCopy);
                ReportActionOutcome(result, nameCopy);
            });

        dialog.ShowAsync();
    }

    void ProcessesView::ReportActionOutcome(platform::ProcessActionResult const& result, std::wstring_view subject)
    {
        if (m_actionMessage == nullptr)
        {
            return;
        }

        winrt::hstring message;

        if (result.Succeeded())
        {
            // The count matters for a tree, where the user asked for one thing and several ended.
            if (result.affected > 1)
            {
                message = winrt::hstring{std::to_wstring(result.affected) + L" processes ended."};
            }
            else
            {
                message = winrt::hstring{winrt::hstring{subject} + L" ended."};
            }
        }
        else
        {
            message = winrt::hstring{winrt::hstring{subject} + L": " + winrt::to_hstring(result.message)};
        }

        m_actionMessage.Text(message);

        // Asking the sampler for a fresh snapshot is what makes the list reflect the change; the
        // application does that as part of acting.
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

    void ProcessesView::_updateHeaderAggregates()
    {
        if (m_aggregates.empty())
        {
            return;
        }

        // The machine-wide figures come from the system snapshot rather than from summing the rows: the
        // rows are a filtered view, and a total that changed when the user typed in the search box would
        // be measuring the filter rather than the machine.
        domain::SystemView const system = m_coordinator.CurrentSystem();

        for (AggregateCell& cell : m_aggregates)
        {
            if (cell.text == nullptr)
            {
                continue;
            }

            std::string text;
            switch (cell.column)
            {
                case SortColumn::Cpu:
                    text = system.ratesUnavailable ? UnavailableValue() : FormatProcessCpuPercent(system.cpuPercent);
                    break;

                case SortColumn::Memory:
                    text = FormatPercent(system.memoryUsedPercent);
                    break;

                case SortColumn::Disk:
                    // The mean active time across the devices rather than their sum: two disks each at 40
                    // percent is not 80 percent of anything, and the original reports the machine's disk
                    // activity as a proportion of busy time.
                    text = FormatPercent(system.diskActivePercent);
                    break;

                case SortColumn::Network:
                    // Utilisation against the link speed, which is what the original's percentage means.
                    // With several adapters the busiest is used, for the same reason as the disk.
                    text = FormatPercent(_peakNetworkUtilization(system.networks));
                    break;

                case SortColumn::Gpu:
                    text = system.gpu.available ? FormatProcessCpuPercent(system.gpu.utilizationPercent)
                                                : UnavailableValue();
                    break;

                default:
                    continue;
            }

            cell.text.Text(winrt::to_hstring(text));
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
        _updateHeaderAggregates();

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
