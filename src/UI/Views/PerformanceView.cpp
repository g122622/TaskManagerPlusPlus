#include "UI/WinRTUI.h"

#include "UI/Views/PerformanceView.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::Button;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility;
using winrt::Microsoft::UI::Xaml::Controls::ScrollMode;
using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
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
        /// The aspect is about 2:3, matching the original's thumbnails, which are considerably less
        /// wide than the row they sit in. A wider thumbnail crowds the label beside it.
        constexpr double SPARKLINE_WIDTH = 56.0;
        constexpr double SPARKLINE_HEIGHT = 42.0;

        /// Accent fill for the selected sidebar row.
        constexpr winrt::Windows::UI::Color SELECTION_FILL{0x33, 0x4C, 0xC2, 0xFF};

        /// Sentinel for "this row reports no particular device", used where a section carries no
        /// sub-index.
        constexpr size_t NO_SUB_INDEX = static_cast<size_t>(-1);
    }

    PerformanceView::PerformanceView(core::SamplingCoordinator& coordinator,
                                     core::Settings const& settings,
                                     std::function<void(double)> onSidebarWidthChanged)
        : m_coordinator(coordinator),
          m_settings(settings),
          m_onSidebarWidthChanged(std::move(onSidebarWidthChanged))
    {
        _buildLayout();
        _selectRow(0);

        // Seed every chart from the settings, so the configured colours are what is drawn on the
        // first frame rather than a hardcoded default the settings page would later contradict.
        ApplySettings(m_settings);
    }

    void PerformanceView::SetMiniMode(bool mini)
    {
        if (m_miniMode == mini)
        {
            return;
        }

        m_miniMode = mini;

        if (mini)
        {
            // The sidebar's width is remembered before it stretches, so leaving the mode restores what
            // the user had rather than a default.
            if (m_sidebarColumn != nullptr)
            {
                m_sidebarPixelWidth = m_sidebarColumn.Width();

                // The sidebar takes the whole window in mini mode. It keeps a fixed width otherwise, so
                // leaving it alone would leave the compact window mostly empty to the right of the list.
                m_sidebarColumn.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            }

            // The current widths are remembered before the columns collapse, so leaving the mode
            // restores what the user had rather than a default.
            if (m_pageColumn != nullptr)
            {
                m_pageWidth = m_pageColumn.Width();
                m_pageColumn.Width(GridLengthHelper::FromPixels(0.0));
            }

            if (m_splitterColumn != nullptr)
            {
                m_splitterWidth = m_splitterColumn.Width();
                m_splitterColumn.Width(GridLengthHelper::FromPixels(0.0));
            }

            // The page is removed rather than hidden: a collapsed element is still measured on every
            // layout pass, and the point of the mode is that only the list is left.
            if (m_detailHost != nullptr)
            {
                // C++/WinRT reports the position through an out-parameter and returns whether the
                // element was found.
                uint32_t index = 0;
                if (m_root.Children().IndexOf(m_detailHost, index))
                {
                    m_root.Children().RemoveAt(index);
                }
            }

            if (m_sidebarSplitter != nullptr)
            {
                m_sidebarSplitter.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            }

            // The card fills the window, and the margin that separated it from the page goes with the
            // page.
            //
            // Its card background goes too. The card exists to separate the list from the page beside
            // it; with the page gone there is nothing to separate it from, and a lighter panel behind
            // the rows reads as an unintended highlight covering the whole window.
            if (m_sidebarCard != nullptr)
            {
                m_sidebarCard.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));
                m_sidebarCard.Background(
                    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
                m_sidebarCard.BorderThickness(
                    winrt::Microsoft::UI::Xaml::ThicknessHelper::FromUniformLength(0.0));
            }

            // The list is no longer inset for a neighbouring page, so the rows reach both edges.
            m_root.Padding(ThicknessHelper::FromLengths(0.0, 0.0, 0.0, 0.0));
        }
        else
        {
            if (m_sidebarColumn != nullptr)
            {
                m_sidebarColumn.Width(m_sidebarPixelWidth);
            }

            if (m_pageColumn != nullptr)
            {
                m_pageColumn.Width(m_pageWidth);
            }

            if (m_splitterColumn != nullptr)
            {
                m_splitterColumn.Width(m_splitterWidth);
            }

            if (m_sidebarSplitter != nullptr)
            {
                m_sidebarSplitter.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Visible);
            }

            if (m_detailHost != nullptr && m_detailHost.Parent() == nullptr)
            {
                Grid::SetColumn(m_detailHost, 2);
                m_root.Children().Append(m_detailHost);
            }

            if (m_sidebarCard != nullptr)
            {
                // The card's own appearance is restored from the shared factory rather than from values
                // recorded here, so it cannot drift from what the other pages' cards look like.
                Border const restored = controls::MakeCard();
                m_sidebarCard.Background(restored.Background());
                m_sidebarCard.BorderBrush(restored.BorderBrush());
                m_sidebarCard.BorderThickness(restored.BorderThickness());
                m_sidebarCard.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 14.0, 0.0));
            }

            m_root.Padding(ThicknessHelper::FromLengths(metrics::CONTENT_LEFT_INSET, 8.0,
                                                        metrics::PAGE_MARGIN, 8.0));
        }
    }

    void PerformanceView::ApplySettings(core::Settings const& settings)
    {
        m_settings = settings;

        // The styles are pushed into the sidebar thumbnails and the pages, so a colour change is
        // visible without recreating anything.
        for (size_t i = 0; i < m_rows.size() && i < m_sections.size(); ++i)
        {
            core::ChartStyle const& style = settings.ChartStyleFor(static_cast<int>(m_sections[i].kind));
            winrt::Windows::UI::Color const color{0xFF, style.red, style.green, style.blue};

            if (m_rows[i].sparkline != nullptr)
            {
                m_rows[i].sparkline->SetColors(color, /*muted=*/i != m_selectedRow);
                m_rows[i].sparkline->SetThickness(style.ClampedLineWidth());
            }
        }

        if (m_cpuPage != nullptr)
        {
            core::ChartStyle const& cpu = settings.ChartStyleFor(0);
            m_cpuPage->SetAccentColor(winrt::Windows::UI::Color{0xFF, cpu.red, cpu.green, cpu.blue});
            m_cpuPage->SetLineWidth(cpu.ClampedLineWidth());
        }

        if (m_memoryPage != nullptr)
        {
            core::ChartStyle const& memory = settings.ChartStyleFor(1);
            m_memoryPage->SetAccentColor(winrt::Windows::UI::Color{0xFF, memory.red, memory.green, memory.blue});
            m_memoryPage->SetLineWidth(memory.ClampedLineWidth());
        }

        for (auto const& page : m_diskPages)
        {
            if (page != nullptr)
            {
                core::ChartStyle const& disk = settings.ChartStyleFor(2);
                page->SetAccentColor(winrt::Windows::UI::Color{0xFF, disk.red, disk.green, disk.blue});
                page->SetLineWidth(disk.ClampedLineWidth());
            }
        }

        if (m_networkPage != nullptr)
        {
            core::ChartStyle const& network = settings.ChartStyleFor(3);
            m_networkPage->SetAccentColor(winrt::Windows::UI::Color{0xFF, network.red, network.green, network.blue});
            m_networkPage->SetLineWidth(network.ClampedLineWidth());
        }

        if (m_gpuPage != nullptr)
        {
            core::ChartStyle const& gpu = settings.ChartStyleFor(4);
            m_gpuPage->SetAccentColor(winrt::Windows::UI::Color{0xFF, gpu.red, gpu.green, gpu.blue});
            m_gpuPage->SetLineWidth(gpu.ClampedLineWidth());
        }
    }

    void PerformanceView::_setSidebarWidth(double width)
    {
        // Clamped between a width that still shows the labels and one that leaves the detail area
        // usable. Without a floor the sidebar can be dragged to nothing and the labels become
        // unreachable; without a ceiling it can swallow the charts the page exists to show.
        constexpr double MIN_SIDEBAR = 180.0;
        constexpr double MAX_SIDEBAR = 520.0;

        double const clamped = std::clamp(width, MIN_SIDEBAR, MAX_SIDEBAR);
        if (std::abs(clamped - m_sidebarWidth) < 0.5)
        {
            return;
        }
        m_sidebarWidth = clamped;

        // The column owns the width; the drag reports a value and this is the one place it is
        // applied, so the column and the persisted width cannot diverge.
        if (m_root.ColumnDefinitions().Size() > 0)
        {
            m_root.ColumnDefinitions().GetAt(0).Width(GridLengthHelper::FromPixels(clamped));
        }

        if (m_onSidebarWidthChanged)
        {
            m_onSidebarWidthChanged(clamped);
        }
    }

    void PerformanceView::_buildLayout()
    {
        m_root = Grid();
        // A smaller left inset than a normal page: the navigation rail already separates the content
        // from the window edge, so the full page margin leaves a conspicuous gap before the sidebar
        // card.
        m_root.Padding(ThicknessHelper::FromLengths(metrics::CONTENT_LEFT_INSET, 8.0, metrics::PAGE_MARGIN, 8.0));

        // Two columns separated by a draggable handle: the sidebar at a width the user can change,
        // and the detail area taking everything that remains. A star column is the equivalent of
        // calc(100% - sidebarWidth) and, unlike a hard-coded width, keeps working when the window is
        // resized.
        double const sidebarWidth = (m_settings.performanceSidebarWidth > 0.0)
                                        ? m_settings.performanceSidebarWidth
                                        : metrics::PERFORMANCE_SIDEBAR_WIDTH;
        m_sidebarWidth = sidebarWidth;

        // Kept as a member because mini mode changes its width and has to put it back.
        m_sidebarColumn = controls::MakeResizableColumn(m_sidebarSplitter,
                                                        [this](double width) { _setSidebarWidth(width); },
                                                        sidebarWidth);
        m_root.ColumnDefinitions().Append(m_sidebarColumn);

        // The handle sits in its own column of zero width, aligned to the boundary. Putting it inside
        // the sidebar column would let it be clipped when the column narrows.
        m_splitterColumn = ColumnDefinition();
        m_splitterColumn.Width(GridLengthHelper::FromPixels(0.0));
        m_root.ColumnDefinitions().Append(m_splitterColumn);

        m_pageColumn = ColumnDefinition();
        m_pageColumn.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
        m_root.ColumnDefinitions().Append(m_pageColumn);

        m_sidebarSplitter.VerticalAlignment(VerticalAlignment::Stretch);
        Grid::SetColumn(m_sidebarSplitter, 1);
        m_root.Children().Append(m_sidebarSplitter);

        // --- Sidebar -----------------------------------------------------------
        //
        // The card is created here and its row container is filled by _rebuildSidebarIfNeeded, which
        // is what lets the list follow the machine's devices.
        m_sidebarCard = controls::MakeCard();
        m_sidebarCard.Padding(ThicknessHelper::FromLengths(3.0, 3.0, 3.0, 3.0));
        m_sidebarCard.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 14.0, 0.0));

        // Stretch rather than Top: a card sized to its content grows past the bottom of a short window
        // and its last rows are simply not drawn, with nothing to say they exist. Filling the column
        // gives the scroll area inside it a bounded height to scroll within.
        m_sidebarCard.VerticalAlignment(VerticalAlignment::Stretch);
        m_sidebarCard.HorizontalAlignment(HorizontalAlignment::Stretch);

        // The rows are scrolled rather than clipped. This is safe here, unlike on a chart page: the
        // sidebar is a stack of fixed-height buttons, so nothing inside depends on a star row resolving
        // against the available height, which is what a ScrollViewer's unlimited height breaks.
        //
        // Vertical only, and the bar is left to appear on its own: a machine with few devices shows
        // nothing, and one with many shows the affordance exactly when the list overflows.
        ScrollViewer sidebarScroll;
        sidebarScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        sidebarScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        sidebarScroll.VerticalScrollMode(ScrollMode::Auto);
        sidebarScroll.HorizontalScrollMode(ScrollMode::Disabled);

        m_sidebar = controls::MakeStack(1.0);
        sidebarScroll.Content(m_sidebar);
        m_sidebarCard.Child(sidebarScroll);

        Grid::SetColumn(m_sidebarCard, 0);
        m_root.Children().Append(m_sidebarCard);

        // Double-clicking the sidebar asks for the compact window. The view reports the gesture rather
        // than performing it: resizing the window it lives in is not something a page can do.
        m_sidebarCard.DoubleTapped(
            [this](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Microsoft::UI::Xaml::Input::DoubleTappedRoutedEventArgs const&) {
                if (m_miniModeHandler)
                {
                    m_miniModeHandler();
                }
            });

        // --- Detail area -------------------------------------------------------
        m_detailHost = Grid();
        Grid::SetColumn(m_detailHost, 2);
        m_root.Children().Append(m_detailHost);
    }

    bool PerformanceView::_rebuildSidebarIfNeeded(domain::SystemView const& system)
    {
        // Builds the row list from the machine. CPU, memory and GPU are single rows because there is
        // one of each; disks and network adapters are one row per device, because a machine has any
        // number of them and showing only the first reads as though the others did not exist.
        std::vector<SectionSpec> next;

        next.push_back(SectionSpec{L"CPU", L"\xE950", SectionKind::Cpu, true, 0});
        next.push_back(SectionSpec{L"Memory", L"\xEEA0", SectionKind::Memory, true, 0});

        // The device rows are taken from the last sample that reported any. A sample that reports none is
        // a failed read rather than an observation that the hardware is gone, and rebuilding the list
        // from it would make every disk and network row disappear and come back -- which is what a user
        // sees as the items flickering out of the sidebar.
        if (!system.disks.empty())
        {
            m_knownDiskCount = system.disks.size();
        }

        // The order the rows appear in. A disk is named by the volumes it backs, so ordering by the first
        // letter and ordering by the device index give genuinely different lists rather than the same one
        // reversed: the device the firmware calls 2 may be the one backing C:.
        if (m_diskOrder.size() != system.disks.size() || m_diskOrderSettings != m_settings.diskSortOrder)
        {
            m_diskOrder.resize(system.disks.size());
            for (size_t i = 0; i < m_diskOrder.size(); ++i)
            {
                m_diskOrder[i] = i;
            }

            if (m_settings.diskSortOrder == core::DiskSortOrder::FirstDriveLetter)
            {
                std::stable_sort(m_diskOrder.begin(), m_diskOrder.end(),
                                 [&system](size_t a, size_t b) {
                                     // Compared by the first volume letter the device backs. A device with
                                     // no letters sorts last, since there is nothing to compare it by.
                                     auto const firstLetter = [&system](size_t index) -> wchar_t {
                                         for (wchar_t const c : system.disks[index].instanceName)
                                         {
                                             if (c >= L'A' && c <= L'Z')
                                             {
                                                 return c;
                                             }
                                         }
                                         return L'[';  // Sorts after every letter.
                                     };
                                     return firstLetter(a) < firstLetter(b);
                                 });
            }

            m_diskOrderSettings = m_settings.diskSortOrder;
        }
        if (!system.networks.empty())
        {
            m_knownNetworkCount = system.networks.size();
        }

        // The rows are built from the known counts, not from the current sample's list. A sample with
        // fewer devices than were last seen is a failed read, and the rows are kept until one arrives
        // that reports them; the figures for a device that has genuinely gone show as unavailable until
        // the next sample removes its row.
        for (size_t row = 0; row < m_knownDiskCount; ++row)
        {
            // The row's position and the device it reports are different things once the order is not
            // the device index: the row index is what the sidebar shows, and subIndex is what the page
            // opens.
            size_t const device = (row < m_diskOrder.size()) ? m_diskOrder[row] : row;

            std::wstring title = L"Disk ";
            if (device < system.disks.size())
            {
                // The instance name is the form the original uses: the device index followed by the
                // volumes it backs, as in "2 C: D:".
                title += winrt::to_hstring(system.disks[device].instanceName).c_str();
                m_diskTitles[device] = title;
            }
            else if (auto const known = m_diskTitles.find(device); known != m_diskTitles.end())
            {
                title = known->second;
            }
            else
            {
                title += std::to_wstring(device);
            }

            next.push_back(SectionSpec{std::move(title), L"\xEDA2", SectionKind::Disk, true, device});
        }

        for (size_t i = 0; i < m_knownNetworkCount; ++i)
        {
            // The row is named by its medium rather than by its chipset. An adapter's description is a
            // long part number that says nothing a reader wants from a sidebar; "Wi-Fi" and "Ethernet"
            // are what the two kinds of connection are called, and that is the distinction the row is
            // for.
            std::wstring name = L"Network";
            if (i < system.networks.size())
            {
                name = system.networks[i].wireless ? L"Wi-Fi" : L"Ethernet";
                m_networkTitles[i] = name;
            }
            else if (auto const known = m_networkTitles.find(i); known != m_networkTitles.end())
            {
                name = known->second;
            }

            next.push_back(SectionSpec{std::move(name), L"\xE968", SectionKind::Network, true, i});
        }

        next.push_back(SectionSpec{L"GPU", L"\xE7F4", SectionKind::Gpu, true, 0});

        // Compared by title and sub-index rather than rebuilt unconditionally: rebuilding tears down
        // the buttons, which would lose the selection and reset every thumbnail on each frame.
        bool const same = next.size() == m_sections.size() &&
                          std::equal(next.begin(), next.end(), m_sections.begin(),
                                     [](SectionSpec const& a, SectionSpec const& b) {
                                         return a.title == b.title && a.kind == b.kind && a.subIndex == b.subIndex;
                                     });
        if (same)
        {
            return false;
        }


        m_sections = std::move(next);

        m_sidebar.Children().Clear();
        m_rows.clear();
        m_rows.reserve(m_sections.size());

        for (size_t i = 0; i < m_sections.size(); ++i)
        {
            SectionSpec const& spec = m_sections[i];

            // Row layout: the row's chart on the left, then the name and its qualifier stacked. This
            // is the arrangement the original uses, and it is what lets one row carry a name, a
            // current value and a trend without any of them crowding the others.
            Grid rowContent = Grid();

            ColumnDefinition thumbColumn;
            thumbColumn.Width(GridLengthHelper::FromPixels(SPARKLINE_WIDTH));
            rowContent.ColumnDefinitions().Append(thumbColumn);

            ColumnDefinition textColumn;
            textColumn.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            rowContent.ColumnDefinitions().Append(textColumn);

            // Constructed with the configured colour rather than a placeholder, which is what had
            // drawn the thumbnails black until a selection change repainted them.
            auto sparkline = std::make_unique<Sparkline>(_sectionColor(spec.kind), SPARKLINE_WIDTH, SPARKLINE_HEIGHT);
            sparkline->Root().VerticalAlignment(VerticalAlignment::Center);
            Grid::SetColumn(sparkline->Root(), 0);
            rowContent.Children().Append(sparkline->Root());

            StackPanel text = controls::MakeStack(0.0);
            text.VerticalAlignment(VerticalAlignment::Center);
            text.Margin(ThicknessHelper::FromLengths(8.0, 0.0, 0.0, 0.0));

            TextBlock title = controls::MakeText(spec.title, 14.0);
            title.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            title.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

            // The qualifier line carries the current reading, or says plainly that the metric is not
            // collected yet.
            TextBlock subtitle = controls::MakeText(spec.hasData ? L"" : L"-- not collected yet", 12.0, true);
            subtitle.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            subtitle.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

            // A third band, used by the memory row to report its commit charge. Every row carries the
            // block so the rows keep one layout, and a row with no third reading leaves it collapsed --
            // which costs nothing, because the stack is centred in the row and a collapsed child takes no
            // space.
            TextBlock detail = controls::MakeText(L"", 12.0, true);
            detail.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
            detail.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
            detail.Visibility(winrt::Microsoft::UI::Xaml::Visibility::Collapsed);

            text.Children().Append(title);
            text.Children().Append(subtitle);
            text.Children().Append(detail);

            Grid::SetColumn(text, 1);
            rowContent.Children().Append(text);

            Button button;
            button.Content(rowContent);
            button.Height(metrics::SIDEBAR_ROW_HEIGHT);
            // The hover fill is a rounded box around the row, so its horizontal padding is what puts the
            // highlight's edge a readable distance from the text. Six pixels left the fill's corners
            // almost touching the thumbnail and the text.
            button.Padding(ThicknessHelper::FromLengths(metrics::SIDEBAR_ROW_PADDING, 0.0,
                                                        metrics::SIDEBAR_ROW_PADDING, 0.0));
            // Stretch makes the hover fill span the sidebar rather than shrink to fit its content, which
            // is what a list of rows should do: every row's highlight is the same width.
            button.HorizontalAlignment(HorizontalAlignment::Stretch);
            button.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            button.Background(SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            button.BorderThickness(ThicknessHelper::FromUniformLength(0.0));

            // The row index is captured by value: the handler must open the row it was built for, and
            // the list can be rebuilt after it is attached.
            button.Click([this, i](winrt::Windows::Foundation::IInspectable const&,
                                   winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) { _selectRow(i); });

            SidebarRow row;
            row.button = button;
            row.title = title;
            row.subtitle = subtitle;
            row.detail = detail;
            row.sparkline = std::move(sparkline);
            row.kind = spec.kind;
            row.subIndex = spec.subIndex;

            m_rows.push_back(std::move(row));
            m_sidebar.Children().Append(button);
        }

        // The selection is restored by kind and sub-index, so rebuilding the list does not move the
        // user to a different page.
        m_selectedRow = 0;
        for (size_t i = 0; i < m_sections.size(); ++i)
        {
            if (m_sections[i].kind == m_selectedKind &&
                (m_sections[i].subIndex == m_selectedSubIndex || m_sections[i].kind == SectionKind::Cpu ||
                 m_sections[i].kind == SectionKind::Memory || m_sections[i].kind == SectionKind::Gpu))
            {
                m_selectedRow = i;
                break;
            }
        }

        _updateSelectionVisuals();
        return true;
    }

    winrt::Windows::UI::Color PerformanceView::_sectionColor(SectionKind kind) const
    {
        // Read from the settings rather than a table: the settings page is the only source, and a
        // second hardcoded copy is what makes a colour change appear to do nothing.
        core::ChartStyle const& style = m_settings.ChartStyleFor(static_cast<int>(kind));
        return winrt::Windows::UI::Color{0xFF, style.red, style.green, style.blue};
    }

    void PerformanceView::_updateSelectionVisuals()
    {
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
            bool const selected = (i == m_selectedRow);

            // The selected row is filled with a translucent accent. Its sparkline goes to full
            // opacity and the others are muted, so the current section's trend is the one that
            // stands out.
            m_rows[i].button.Background(
                SolidColorBrush(selected ? SELECTION_FILL : winrt::Windows::UI::Colors::Transparent()));
            m_rows[i].button.CornerRadius(
                winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(metrics::CONTROL_RADIUS));

            if (m_rows[i].sparkline != nullptr)
            {
                m_rows[i].sparkline->SetColors(_sectionColor(m_rows[i].kind), /*muted=*/!selected);
            }
        }
    }

    void PerformanceView::_selectRow(size_t rowIndex)
    {
        if (rowIndex >= m_sections.size())
        {
            return;
        }

        m_selectedRow = rowIndex;
        m_selectedKind = m_sections[rowIndex].kind;
        m_selectedSubIndex = m_sections[rowIndex].subIndex;

        m_detailHost.Children().Clear();
        m_detailsCard = nullptr;
        m_detailValues.clear();

        _updateSelectionVisuals();

        // Each page is handed the style it should draw with as it is built.
        //
        // Taking it here rather than telling the page afterwards is what makes this correct by
        // construction: the page applies it in its own constructor, so there is no window in which a
        // chart exists with the defaults and nothing has yet said otherwise.
        core::ChartStyle const& style = m_settings.ChartStyleFor(static_cast<int>(m_selectedKind));

        // One page per kind, created on first selection and reused, so switching away and back does
        // not rebuild a chart and flash it empty.
        switch (m_selectedKind)
        {
            case SectionKind::Cpu:
                if (m_cpuPage == nullptr)
                {
                    m_cpuPage = std::make_unique<CpuPage>(m_coordinator, style);
                }
                m_detailHost.Children().Append(m_cpuPage->Root());
                break;

            case SectionKind::Memory:
                if (m_memoryPage == nullptr)
                {
                    m_memoryPage = std::make_unique<MemoryPage>(m_coordinator, style);
                }
                m_detailHost.Children().Append(m_memoryPage->Root());
                break;

            case SectionKind::Disk:
            {
                // One page per device, grown on demand. A machine's disk count does not change, so
                // the vector only ever grows.
                while (m_diskPages.size() <= m_selectedSubIndex)
                {
                    m_diskPages.push_back(std::make_unique<DiskPage>(m_coordinator, style));
                }

                DiskPage& page = *m_diskPages[m_selectedSubIndex];
                page.SetDeviceIndex(m_selectedSubIndex);
                page.SetDeviceLabel(m_sections[rowIndex].title);
                m_detailHost.Children().Append(page.Root());
                break;
            }

            case SectionKind::Network:
                if (m_networkPage == nullptr)
                {
                    m_networkPage = std::make_unique<NetworkPage>(m_coordinator, style);
                }
                m_networkPage->SetAdapterIndex(m_selectedSubIndex);
                m_networkPage->SetAdapterLabel(m_sections[rowIndex].title);
                m_detailHost.Children().Append(m_networkPage->Root());
                break;

            case SectionKind::Gpu:
                if (m_gpuPage == nullptr)
                {
                    m_gpuPage = std::make_unique<GpuPage>(m_coordinator, style);
                }
                m_detailHost.Children().Append(m_gpuPage->Root());
                break;
        }

        // The newly opened page has not drawn anything yet, so the version check must not skip it.
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

        // The optional third reading. It is collapsed when there is nothing to say, so a row that does not
        // use it keeps its two-line layout and does not reserve a blank band.
        auto setDetail = [this](size_t index, std::string const& text) {
            if (index >= m_rows.size() || m_rows[index].detail == nullptr)
            {
                return;
            }

            m_rows[index].detail.Text(winrt::to_hstring(text));
            m_rows[index].detail.Visibility(text.empty() ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                                                       : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        };

        for (size_t i = 0; i < m_rows.size() && i < m_sections.size(); ++i)
        {
            SectionSpec const& spec = m_sections[i];
            if (m_rows[i].sparkline == nullptr)
            {
                continue;
            }

            std::string subtitle;

            // Cleared before the switch, so only the branch that has something to say shows a third line.
            // Without this a row would keep the reading it was given while it was some other metric, and
            // the sidebar is rebuilt when the device list changes -- so a stale commit charge could outlive
            // the row it belonged to.
            setDetail(i, "");

            switch (spec.kind)
            {
                case SectionKind::Cpu:
                {
                    subtitle = system.ratesUnavailable ? UnavailableValue() : FormatPercent(system.cpuPercent);
                    subtitle += "  ";
                    subtitle += system.processorSpeed.available
                                    ? std::to_string(system.processorSpeed.currentMhz) + " MHz"
                                    : std::string{UnavailableValue()};

                    ChartSeries series;
                    series.values = history.cpuTotal;
                    series.windowSamples = history.windowSamples;
                    m_rows[i].sparkline->SetSeries(series, 100.0);
                    break;
                }

                case SectionKind::Memory:
                {
                    subtitle = FormatBytes(system.memoryUsedBytes) + " / " +
                               FormatBytes(system.memory.totalPhysical);
                    subtitle += " (" + FormatPercent(system.memoryUsedPercent) + ")";

                    // The commit charge, which is the figure that decides whether the machine can still
                    // allocate: it can exceed the physical total because it counts what has been promised
                    // to every process, backed or not. Reported as a value against its limit rather than
                    // as a percentage, because the limit is what moves -- a page file that grows changes
                    // it -- so the pair says more than the ratio would.
                    if (system.memory.kernelAccountingAvailable)
                    {
                        setDetail(i, "Commit: " + FormatBytes(system.memory.committedBytes) + " / " +
                                         FormatBytes(system.memory.commitLimitBytes));
                    }
                    else
                    {
                        setDetail(i, "Commit: " + std::string{UnavailableValue()});
                    }

                    ChartSeries series;
                    series.values = history.memoryUsed;
                    series.windowSamples = history.windowSamples;
                    m_rows[i].sparkline->SetSeries(series, 100.0);
                    break;
                }

                case SectionKind::Disk:
                {
                    if (spec.subIndex >= system.disks.size())
                    {
                        setSubtitle(i, std::string{UnavailableValue()} + " unavailable");
                        m_rows[i].sparkline->Clear();
                        break;
                    }

                    domain::DiskActivity const& disk = system.disks[spec.subIndex];

                    // The row states the device's throughput in both directions, which is what a user
                    // reads a disk by. The active percentage is a summary of the same thing and says
                    // nothing about how much data moved, so it is not what the row should lead with.
                    subtitle = "R: " + FormatBytes(static_cast<uint64_t>(disk.readBytesPerSecond)) + "/s";
                    subtitle += "  W: " + FormatBytes(static_cast<uint64_t>(disk.writeBytesPerSecond)) + "/s";

                    // The device's own series, looked up by instance name. Every disk has its own
                    // history, so a machine with several disks shows each one's trend rather than one
                    // trend repeated on every row. The two directions are summed for the thumbnail,
                    // which has no room for two lines.
                    auto const seriesForDevice = history.diskBytesPerSecondByDevice.find(disk.instanceName);
                    if (seriesForDevice != history.diskBytesPerSecondByDevice.end())
                    {
                        ChartSeries series;
                        series.values = seriesForDevice->second.first;
                        for (size_t k = 0;
                             k < series.values.size() && k < seriesForDevice->second.second.size();
                             ++k)
                        {
                            series.values[k] += seriesForDevice->second.second[k];
                        }
                        series.windowSamples = history.windowSamples;

                        double peak = 0.0;
                        for (double const value : series.values)
                        {
                            peak = (std::max)(peak, value);
                        }
                        m_rows[i].sparkline->SetSeries(series, peak > 0.0 ? peak : 1.0);
                    }
                    else
                    {
                        m_rows[i].sparkline->Clear();
                    }
                    break;
                }

                case SectionKind::Network:
                {
                    if (spec.subIndex >= system.networks.size())
                    {
                        setSubtitle(i, std::string{UnavailableValue()} + " unavailable");
                        m_rows[i].sparkline->Clear();
                        break;
                    }

                    domain::NetworkActivity const& iface = system.networks[spec.subIndex];
                    subtitle = "S: " + FormatBytes(static_cast<uint64_t>(iface.sentBytesPerSecond)) + "/s";
                    subtitle += "  R: " + FormatBytes(static_cast<uint64_t>(iface.receivedBytesPerSecond)) + "/s";

                    // The adapter's own series, looked up by name, for the same reason the disk rows
                    // use theirs.
                    auto const seriesForAdapter = history.networkBytesPerSecondByAdapter.find(iface.adapterName);
                    if (seriesForAdapter != history.networkBytesPerSecondByAdapter.end())
                    {
                        ChartSeries series;
                        series.values = seriesForAdapter->second.first;
                        for (size_t k = 0;
                             k < series.values.size() && k < seriesForAdapter->second.second.size();
                             ++k)
                        {
                            series.values[k] += seriesForAdapter->second.second[k];
                        }
                        series.windowSamples = history.windowSamples;

                        double peak = 0.0;
                        for (double const value : series.values)
                        {
                            peak = (std::max)(peak, value);
                        }
                        m_rows[i].sparkline->SetSeries(series, peak > 0.0 ? peak : 1.0);
                    }
                    else
                    {
                        m_rows[i].sparkline->Clear();
                    }
                    break;
                }

                case SectionKind::Gpu:
                {
                    if (!system.gpu.available)
                    {
                        setSubtitle(i, std::string{UnavailableValue()} + " not collected yet");
                        m_rows[i].sparkline->Clear();
                        break;
                    }

                    subtitle = FormatPercent(system.gpu.utilizationPercent);
                    if (system.gpu.dedicatedTotalBytes > 0)
                    {
                        subtitle += "  " + FormatBytes(system.gpu.dedicatedUsedBytes);
                    }

                    ChartSeries series;
                    series.values = history.gpuUtilization;
                    series.windowSamples = history.windowSamples;
                    m_rows[i].sparkline->SetSeries(series, 100.0);
                    break;
                }
            }

            if (!subtitle.empty())
            {
                setSubtitle(i, subtitle);
            }
        }
    }

    void PerformanceView::Refresh()
    {
        // Version first, for the same reason as the process list: the copies below are far more
        // expensive than comparing a number, and the UI polls much more often than the sampler
        // publishes.
        uint64_t const version = m_coordinator.SystemVersion();
        if (version == m_renderedVersion && m_renderedVersion != 0)
        {
            return;
        }

        domain::SystemView const system = m_coordinator.CurrentSystem();
        domain::HistoryView const history = m_coordinator.CurrentHistory();
        m_renderedVersion = system.version;

        // The list follows the machine: an empty device list on the first frames becomes a row per
        // disk once the counters are readable.
        if (_rebuildSidebarIfNeeded(system))
        {
            // The selected row's page is opened here, and only here on a rebuild, because it could not
            // be opened in the constructor: the sidebar is built from the first sample, so at
            // construction there was no row to select and the detail area stayed empty until the user
            // clicked something.
            _selectRow(m_selectedRow);

            // A rebuilt list has fresh, empty charts, so the values are written before returning.
            _updateSidebarValues(system, history);
            return;
        }

        _updateSidebarValues(system, history);

        switch (m_selectedKind)
        {
            case SectionKind::Cpu:
                if (m_cpuPage != nullptr)
                {
                    m_cpuPage->Refresh();
                }
                break;

            case SectionKind::Memory:
                if (m_memoryPage != nullptr)
                {
                    m_memoryPage->Refresh();
                }
                break;

            case SectionKind::Disk:
                if (m_selectedSubIndex < m_diskPages.size() && m_diskPages[m_selectedSubIndex] != nullptr)
                {
                    m_diskPages[m_selectedSubIndex]->Refresh();
                }
                break;

            case SectionKind::Network:
                if (m_networkPage != nullptr)
                {
                    m_networkPage->Refresh();
                }
                break;

            case SectionKind::Gpu:
                if (m_gpuPage != nullptr)
                {
                    m_gpuPage->Refresh();
                }
                break;
        }
    }
}
