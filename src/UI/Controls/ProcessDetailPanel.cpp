#include "UI/WinRTUI.h"

#include "UI/Controls/ProcessDetailPanel.h"

#include "UI/Theming/Controls.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include "UI/Diagnostics.h"

#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <map>
#include <string>
#include <string_view>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::Button;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::ComboBox;
using winrt::Microsoft::UI::Xaml::Controls::ComboBoxItem;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility;
using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::HorizontalAlignment;
using winrt::Microsoft::UI::Xaml::TextWrapping;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;
using winrt::Microsoft::UI::Xaml::Visibility;

namespace tmpp::ui
{
    namespace
    {
        // --------------------------------------------------------------------
        // Layout constants
        // --------------------------------------------------------------------

        /// Label width in the priority grid. Wide enough for "Efficiency" at the body size, because
        /// the labels form a column and one that does not fit pushes its own control out of line.
        constexpr double LABEL_COLUMN_WIDTH = 92.0;

        /// Height of one priority row, so the four rows are evenly spaced whatever height each
        /// control happens to report.
        constexpr double PRIORITY_ROW_SPACING = 8.0;

        /// Font size used inside a core chip and for the caption lines.
        constexpr double SMALL_FONT_SIZE = 12.0;

        /// Corner radius of a core chip.
        constexpr double CHIP_RADIUS = 4.0;

        /// Fill of a selected core chip. The accent is deliberately not used: a selected chip is a
        /// state rather than an action, and the accent is reserved for the buttons that do something.
        constexpr winrt::Windows::UI::Color CHIP_SELECTED_FILL{0xFF, 0x2D, 0x6C, 0xC8};

        /// Fill of an unselected chip: the layer colour, which is a shade off the card underneath and
        /// therefore visible as a chip without competing with the text.
        constexpr winrt::Windows::UI::Color CHIP_FILL{0x33, 0x80, 0x80, 0x80};

        // --------------------------------------------------------------------
        // Option tables
        // --------------------------------------------------------------------
        //
        // The combo boxes are index-mapped rather than tag-mapped. A tag would carry the value as a
        // boxed object and require a cast back on every read, and the value of a combo box built from
        // a fixed table is its index: the table is the single place the order is defined, so the
        // index cannot come adrift from the value the way a parallel list of tags can.

        struct CpuOption
        {
            wchar_t const* label;
            platform::ProcessPriorityClass value;
        };

        /// Realtime is deliberately absent. It is a class the kernel will grant, and granting it lets
        /// a process starve the input and disk threads of the whole machine. Windows' own task manager
        /// offers it only as a warning-gated advanced option, and offering it beside "High" in a list
        /// would invite the one choice here that can make the machine unusable.
        constexpr CpuOption CPU_OPTIONS[] = {
            {L"Low", platform::ProcessPriorityClass::Idle},
            {L"Below normal", platform::ProcessPriorityClass::BelowNormal},
            {L"Normal", platform::ProcessPriorityClass::Normal},
            {L"Above normal", platform::ProcessPriorityClass::AboveNormal},
            {L"High", platform::ProcessPriorityClass::High},
        };

        struct MemoryOption
        {
            wchar_t const* label;
            platform::ProcessMemoryPriority value;
        };

        constexpr MemoryOption MEMORY_OPTIONS[] = {
            {L"Very low", platform::ProcessMemoryPriority::VeryLow},
            {L"Low", platform::ProcessMemoryPriority::Low},
            {L"Medium", platform::ProcessMemoryPriority::Medium},
            {L"Below normal", platform::ProcessMemoryPriority::BelowNormal},
            {L"Normal", platform::ProcessMemoryPriority::Normal},
        };

        struct IoOption
        {
            wchar_t const* label;
            platform::ProcessIoPriority value;
        };

        constexpr IoOption IO_OPTIONS[] = {
            {L"Very low", platform::ProcessIoPriority::VeryLow},
            {L"Low", platform::ProcessIoPriority::Low},
            {L"Normal", platform::ProcessIoPriority::Normal},
            {L"High", platform::ProcessIoPriority::High},
        };

        struct EfficiencyOption
        {
            wchar_t const* label;
            platform::ProcessEfficiencyMode value;
        };

        constexpr EfficiencyOption EFFICIENCY_OPTIONS[] = {
            {L"Windows decides", platform::ProcessEfficiencyMode::SystemManaged},
            {L"On", platform::ProcessEfficiencyMode::On},
            {L"Off", platform::ProcessEfficiencyMode::Off},
        };

        /// The affinity presets offered as buttons above the core grid.
        enum class AffinityPreset
        {
            All,
            None,
            Invert,
            PrimaryThreads,
            PerformanceCores,
            EfficiencyCores,
        };

        struct PresetSpec
        {
            wchar_t const* label;
            wchar_t const* tooltip;
            AffinityPreset preset;

            /// Only offered on a hybrid processor, where the distinction means something.
            bool hybridOnly;
        };

        constexpr PresetSpec PRESETS[] = {
            {L"All", L"Let the process run on every processor.", AffinityPreset::All, false},
            {L"None", L"Select no processors, then choose the ones to allow.", AffinityPreset::None, false},
            {L"Invert", L"Swap the selected and unselected processors.", AffinityPreset::Invert, false},
            {L"One per core", L"One thread of each physical core, which avoids SMT contention.",
             AffinityPreset::PrimaryThreads, false},
            {L"P-cores", L"Performance cores only.", AffinityPreset::PerformanceCores, true},
            {L"E-cores", L"Efficiency cores only.", AffinityPreset::EfficiencyCores, true},
        };

        /// The always-visible helper text for the suspend control.
        constexpr wchar_t const* SUSPEND_NOTE =
            L"Suspending stops every thread without ending the process. Some drivers and games cannot "
            L"recover from it; save your work first.";

        // --------------------------------------------------------------------
        // Helpers
        // --------------------------------------------------------------------

        [[nodiscard]] std::wstring _toWide(std::string_view utf8)
        {
            return winrt::to_hstring(utf8).c_str();
        }

        /**
         * @brief Packs a process's identity into one value.
         *
         * The same scheme the process model uses for its own state. A pid alone cannot identify a
         * process, because Windows hands a pid to a new process the moment the old one exits, and the
         * undo history is keyed on identity for exactly that reason: restoring a previous process's
         * settings into whatever inherited its number would be worse than losing the history.
         */
        [[nodiscard]] uint64_t _identityKeyOf(uint32_t pid, uint64_t createTime) noexcept
        {
            return (static_cast<uint64_t>(pid) << 32) ^ createTime;
        }

        /// The bit for a logical processor, or zero when it is outside the mask's range.
        [[nodiscard]] uint64_t _maskBit(uint32_t logicalIndex) noexcept
        {
            return logicalIndex < 64 ? (uint64_t{1} << logicalIndex) : 0;
        }

        [[nodiscard]] std::string _lowered(std::string_view value)
        {
            std::string lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lowered;
        }

        [[nodiscard]] std::wstring _formatMask(uint64_t mask, uint32_t groupSize)
        {
            std::wstring text{L"0x"};
            wchar_t buffer[20]{};
            swprintf_s(buffer, L"%llX", static_cast<unsigned long long>(mask));
            text += buffer;

            uint32_t const selected = static_cast<uint32_t>(std::popcount(mask));
            text += L"  (";
            text += std::to_wstring(selected);
            text += L" of ";
            text += std::to_wstring(groupSize);
            text += L" processors)";
            return text;
        }

        /// Appends one option to a combo box, carrying no tag: the index is the value.
        void _appendOption(ComboBox const& box, wchar_t const* label)
        {
            ComboBoxItem item;
            item.Content(winrt::box_value(winrt::hstring{label}));
            box.Items().Append(item);
        }

        /**
         * @brief One row of the priority grid: a label, a control and an apply button.
         *
         * The same three-column shape for all four rows, so their controls line up. The apply button
         * is per row rather than one for the whole card because the four settings live behind four
         * different APIs and one of them can be refused while the others are accepted -- a single
         * Apply would have to report a failure against a row the user had not touched.
         */
        void _addPriorityRow(Grid const& host,
                             int32_t row,
                             wchar_t const* label,
                             winrt::Microsoft::UI::Xaml::FrameworkElement const& control,
                             Button const& apply,
                             wchar_t const* tooltip)
        {
            TextBlock caption = controls::MakeText(label, 13.0);
            caption.VerticalAlignment(VerticalAlignment::Center);
            Grid::SetRow(caption, row);
            Grid::SetColumn(caption, 0);
            host.Children().Append(caption);

            control.HorizontalAlignment(HorizontalAlignment::Stretch);
            Grid::SetRow(control, row);
            Grid::SetColumn(control, 1);
            host.Children().Append(control);

            apply.MinWidth(64.0);
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                apply, winrt::box_value(winrt::hstring{tooltip}));
            Grid::SetRow(apply, row);
            Grid::SetColumn(apply, 2);
            host.Children().Append(apply);
        }
    }

    // ========================================================================
    // Construction
    // ========================================================================

    ProcessDetailPanel::ProcessDetailPanel(core::SamplingCoordinator& coordinator)
        : m_coordinator(coordinator)
    {
        platform::ProcessorTopology const& topology = m_coordinator.ProcessorTopology();
        m_hybrid = topology.hybrid;

        // How many logical processors the affinity mask can address, and which class of core counts as
        // a performance core. Taken from the topology rather than from a second platform query, and
        // counted rather than assumed: a machine with SMT disabled and a machine with it enabled both
        // report a full set of logical processors, but a processor group is sized by what the firmware
        // actually presents.
        uint32_t groupSize = 0;
        uint8_t lowestClass = 0xFF;
        uint8_t highestClass = 0;
        for (platform::ProcessorCore const& core : topology.logicalProcessors)
        {
            if (core.group != 0)
            {
                continue;
            }
            ++groupSize;
            lowestClass = (std::min)(lowestClass, core.efficiencyClass);
            highestClass = (std::max)(highestClass, core.efficiencyClass);
        }

        // Falls back on the machine's logical processor count when the topology walk failed, which
        // leaves the editor usable but without the physical-core grouping.
        m_groupSize = groupSize > 0 ? groupSize : m_coordinator.LogicalProcessorCount();
        if (m_groupSize == 0 || m_groupSize > 64)
        {
            m_groupSize = 64;
        }

        // The performance class is the higher of the two, which is what the firmware reports on the
        // machine this was verified against. See ProcessorCore::efficiencyClass for the evidence; the
        // field does not mean what its name suggests, and reading it the other way round labelled all
        // 28 processors of an i7-14700KF backwards.
        m_performanceClass = (lowestClass == 0xFF) ? 0 : highestClass;

        _buildLayout();
    }

    void ProcessDetailPanel::_buildLayout()
    {
        m_root = ScrollViewer();
        m_root.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        m_root.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);

        m_content = controls::MakeStack(10.0);
        m_content.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 4.0, 0.0));
        m_root.Content(m_content);

        // The feedback line sits above everything and is always present, so it does not push the
        // cards down when a change is applied. Its text is what changes.
        m_status = controls::MakeText(L"", SMALL_FONT_SIZE, true);
        m_status.TextWrapping(TextWrapping::Wrap);
        m_status.Visibility(Visibility::Collapsed);
        m_content.Children().Append(m_status);

        m_placeholder = controls::MakeText(L"Select a process to see and change its settings.", 13.0, true);
        m_placeholder.TextWrapping(TextWrapping::Wrap);
        m_placeholder.Margin(ThicknessHelper::FromLengths(0.0, 8.0, 0.0, 0.0));
        m_content.Children().Append(m_placeholder);

        // Everything below the placeholder is hidden until a process is selected, so the sidebar does
        // not present four cards of dashes on first run.
        m_details = controls::MakeStack(10.0);
        m_details.Visibility(Visibility::Collapsed);
        m_content.Children().Append(m_details);

        _buildSummaryCard(m_details);
        _buildPriorityCard(m_details);
        _buildAffinityCard(m_details);
        _buildAutomationCard(m_details);
    }

    void ProcessDetailPanel::_buildSummaryCard(StackPanel const& host)
    {
        Border card = controls::MakeCard();

        StackPanel content = controls::MakeStack(4.0);

        m_title = controls::MakeHeading(L"", 16.0);
        m_title.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        content.Children().Append(m_title);

        m_path = controls::MakeText(L"", SMALL_FONT_SIZE, true);
        m_path.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        m_path.TextWrapping(TextWrapping::NoWrap);
        content.Children().Append(m_path);

        m_figures = controls::MakeText(L"", 13.0);
        m_figures.Margin(ThicknessHelper::FromLengths(0.0, 4.0, 0.0, 0.0));
        content.Children().Append(m_figures);

        m_stateNote = controls::MakeText(L"", SMALL_FONT_SIZE, true);
        content.Children().Append(m_stateNote);

        card.Child(content);
        host.Children().Append(card);
    }

    void ProcessDetailPanel::_buildPriorityCard(StackPanel const& host)
    {
        Border card = controls::MakeCard();

        StackPanel content = controls::MakeStack(6.0);

        TextBlock heading = controls::MakeHeading(L"Priority", 14.0);
        content.Children().Append(heading);

        TextBlock caption = controls::MakeText(
            L"Each setting is sent on its own, because the operating system can accept one and refuse "
            L"another.",
            SMALL_FONT_SIZE,
            true);
        content.Children().Append(caption);

        Grid grid;
        grid.ColumnDefinitions().Append(controls::MakeFixedColumn(LABEL_COLUMN_WIDTH));
        grid.ColumnDefinitions().Append(controls::MakeStarColumn());
        grid.ColumnDefinitions().Append(controls::MakeAutoColumn());
        grid.RowSpacing(PRIORITY_ROW_SPACING);
        grid.Margin(ThicknessHelper::FromLengths(0.0, 6.0, 0.0, 0.0));

        for (int32_t row = 0; row < 4; ++row)
        {
            grid.RowDefinitions().Append(controls::MakeAutoRow());
        }

        m_cpuBox = ComboBox();
        for (CpuOption const& option : CPU_OPTIONS)
        {
            _appendOption(m_cpuBox, option.label);
        }
        m_cpuApply = controls::MakeButton(L"Apply", [this] { _applyCpuPriority(); });
        _addPriorityRow(grid, 0, L"CPU", m_cpuBox, m_cpuApply, L"Relative share of processor time.");

        m_memoryBox = ComboBox();
        for (MemoryOption const& option : MEMORY_OPTIONS)
        {
            _appendOption(m_memoryBox, option.label);
        }
        m_memoryApply = controls::MakeButton(L"Apply", [this] { _applyMemoryPriority(); });
        _addPriorityRow(grid,
                        1,
                        L"Memory",
                        m_memoryBox,
                        m_memoryApply,
                        L"How long the memory manager keeps this process's pages resident under pressure.");

        m_ioBox = ComboBox();
        for (IoOption const& option : IO_OPTIONS)
        {
            _appendOption(m_ioBox, option.label);
        }
        m_ioApply = controls::MakeButton(L"Apply", [this] { _applyIoPriority(); });
        _addPriorityRow(grid, 2, L"I/O", m_ioBox, m_ioApply, L"Order this process's disk requests are issued in.");

        m_efficiencyBox = ComboBox();
        for (EfficiencyOption const& option : EFFICIENCY_OPTIONS)
        {
            _appendOption(m_efficiencyBox, option.label);
        }
        m_efficiencyApply = controls::MakeButton(L"Apply", [this] { _applyEfficiencyMode(); });
        _addPriorityRow(grid,
                        3,
                        L"Efficiency",
                        m_efficiencyBox,
                        m_efficiencyApply,
                        L"Windows 11 efficiency mode: throttles the process to the efficiency cores.");

        content.Children().Append(grid);

        m_undoButton = controls::MakeButton(L"Undo last change", [this] { _undoLastChange(); });
        m_undoButton.Margin(ThicknessHelper::FromLengths(0.0, 8.0, 0.0, 0.0));
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            m_undoButton,
            winrt::box_value(winrt::hstring{L"Put the settings back the way they were before the most recent change."}));
        content.Children().Append(m_undoButton);

        card.Child(content);
        host.Children().Append(card);
    }

    void ProcessDetailPanel::_buildAffinityCard(StackPanel const& host)
    {
        Border card = controls::MakeCard();

        StackPanel content = controls::MakeStack(6.0);

        TextBlock heading = controls::MakeHeading(L"CPU cores", 14.0);
        content.Children().Append(heading);

        TextBlock caption = controls::MakeText(
            L"Choose the processors this process may run on. The change is sent when you press Apply.",
            SMALL_FONT_SIZE,
            true);
        content.Children().Append(caption);

        // --- Presets -----------------------------------------------------------
        StackPanel presets = controls::MakeRow(6.0);
        presets.Margin(ThicknessHelper::FromLengths(0.0, 4.0, 0.0, 0.0));

        for (size_t i = 0; i < std::size(PRESETS); ++i)
        {
            PresetSpec const& spec = PRESETS[i];
            if (spec.hybridOnly && !m_hybrid)
            {
                continue;
            }

            Button button = controls::MakeButton(spec.label, [this, i] { _applyPreset(i); });
            button.Padding(ThicknessHelper::FromLengths(10.0, 4.0, 10.0, 4.0));
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                button, winrt::box_value(winrt::hstring{spec.tooltip}));
            presets.Children().Append(button);
        }

        content.Children().Append(presets);

        // --- Core chips --------------------------------------------------------
        platform::ProcessorTopology const& topology = m_coordinator.ProcessorTopology();

        // A WrapPanel-like arrangement without a WrapPanel: rows of fixed-height chips inside a
        // vertical stack. A WrapPanel inside a ScrollViewer measures against infinite width and never
        // wraps, which is a known trap; laying the chips out in explicit rows of a fixed count avoids
        // it and keeps every chip the same size.
        constexpr int32_t CHIPS_PER_ROW = 5;

        StackPanel rows = controls::MakeStack(4.0);
        StackPanel currentRow = controls::MakeRow(4.0);

        // What each chip should say and which processor it stands for.
        //
        // Built first and drawn afterwards, so the labelling -- which needs two passes over the
        // topology to number the cores and then their threads -- is separate from the control
        // construction. Interleaving the two is what produced "P0" and "P1" for the two threads of a
        // single physical core.
        struct ChipSpec
        {
            uint32_t logicalIndex{0};
            std::wstring label;
            std::wstring detail;
            bool sharedWithAnotherThread{false};
        };

        std::vector<ChipSpec> chips;

        if (!topology.logicalProcessors.empty())
        {
            // Which kind of core each physical core is, and which one of that kind it is. A user
            // choosing cores is choosing between kinds of core, so the ordinal within the kind is the
            // number they can act on; the machine's own physical index goes in the tooltip, where there
            // is room to be precise.
            std::map<uint32_t, uint32_t> ordinalByPhysicalCore;
            std::map<uint8_t, uint32_t> coresNamedPerClass;

            // How many threads each physical core has, so a single-threaded core is not labelled "T1".
            std::map<uint32_t, uint32_t> threadsByPhysicalCore;

            // The thread number of each logical processor: 1 for the first thread of its core, 2 for
            // the second. Assigned in logical order, which is the order the topology is sorted in.
            std::map<uint32_t, uint32_t> threadNumberByLogicalIndex;
            std::map<uint32_t, uint32_t> threadsSeenPerCore;

            for (platform::ProcessorCore const& core : topology.logicalProcessors)
            {
                if (core.group != 0)
                {
                    continue;
                }

                if (ordinalByPhysicalCore.find(core.physicalCoreIndex) == ordinalByPhysicalCore.end())
                {
                    ordinalByPhysicalCore[core.physicalCoreIndex] = coresNamedPerClass[core.efficiencyClass]++;
                }

                ++threadsByPhysicalCore[core.physicalCoreIndex];
                threadNumberByLogicalIndex[core.logicalIndex] = ++threadsSeenPerCore[core.physicalCoreIndex];
            }

            chips.reserve(topology.logicalProcessors.size());

            for (platform::ProcessorCore const& core : topology.logicalProcessors)
            {
                if (core.group != 0)
                {
                    continue;
                }

                uint32_t const ordinal = ordinalByPhysicalCore[core.physicalCoreIndex];
                uint32_t const threads = threadsByPhysicalCore[core.physicalCoreIndex];
                bool const performance = core.efficiencyClass >= m_performanceClass;

                ChipSpec spec;
                spec.logicalIndex = core.logicalIndex;
                spec.sharedWithAnotherThread = threads > 1;

                // The kind letter and the core's ordinal, then the thread. No "CPU" prefix: the row is
                // introduced by the card's own heading and a caption that says these are processors, so
                // repeating it on every chip is the same word thirty times.
                //
                // "C" when the machine does not distinguish kinds: there is no performance or
                // efficiency class to name, and inventing a letter for one would be worse than saying
                // plainly that this is a core.
                spec.label = m_hybrid ? (performance ? L"P" : L"E") : L"C";
                spec.label += std::to_wstring(ordinal);

                if (threads > 1)
                {
                    spec.label += L" T";
                    spec.label += std::to_wstring(threadNumberByLogicalIndex[core.logicalIndex]);
                }

                // The tooltip carries what the label leaves out, including the machine's own numbering.
                if (m_hybrid)
                {
                    spec.detail = (performance ? L"performance core " : L"efficiency core ") +
                                  std::to_wstring(ordinal);
                }
                else
                {
                    spec.detail = L"physical core " + std::to_wstring(ordinal);
                }
                spec.detail += L" (logical processor " + std::to_wstring(core.logicalIndex);
                if (threads > 1)
                {
                    spec.detail += L", thread " + std::to_wstring(threadNumberByLogicalIndex[core.logicalIndex]) +
                                   L" of " + std::to_wstring(threads);
                }
                spec.detail += L")";

                chips.push_back(std::move(spec));
            }
        }
        else
        {
            // The topology walk failed, so there is no physical grouping to show. The chips still have
            // to exist: the affinity mask is readable and settable without it, and an empty editor
            // would take away a control the machine does support because a presentation detail could
            // not be resolved.
            chips.reserve(m_groupSize);

            for (uint32_t index = 0; index < m_groupSize; ++index)
            {
                ChipSpec spec;
                spec.logicalIndex = index;
                spec.label = std::to_wstring(index);
                spec.detail = L"logical processor " + std::to_wstring(index);
                chips.push_back(std::move(spec));
            }
        }

        for (ChipSpec const& spec : chips)
        {
            Border chip = Border();
            chip.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(CHIP_RADIUS));
            chip.BorderThickness(ThicknessHelper::FromUniformLength(1.0));
            chip.BorderBrush(controls::ThemedBrush(theme::CARD_BORDER));
            chip.Padding(ThicknessHelper::FromLengths(8.0, 4.0, 8.0, 4.0));
            chip.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(CHIP_FILL));

            TextBlock chipLabel = controls::MakeText(spec.label, SMALL_FONT_SIZE);
            chipLabel.TextWrapping(TextWrapping::NoWrap);
            chip.Child(chipLabel);

            uint32_t const logicalIndex = spec.logicalIndex;
            chip.Tapped([this, logicalIndex](winrt::Windows::Foundation::IInspectable const&,
                                             winrt::Microsoft::UI::Xaml::Input::TappedRoutedEventArgs const&) {
                // The pending mask starts from what the process actually has, so the first tap toggles
                // one processor rather than replacing the whole mask with a single bit.
                uint64_t mask = m_pendingAffinity.has_value() ? m_pendingAffinity.value() : m_state.affinityMask;
                mask ^= _maskBit(logicalIndex);
                m_pendingAffinity = mask;
                _refreshChips();
            });

            // The spoken name spells out what the chip's own label abbreviates, because a screen reader
            // announcing "P7 T2" says nothing about what was selected.
            winrt::Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
                chip, winrt::hstring{spec.label + L", " + spec.detail});
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
                chip, winrt::box_value(winrt::hstring{spec.detail}));

            currentRow.Children().Append(chip);
            m_chips.push_back({logicalIndex, chip, chipLabel});
            m_chipIsSmtSibling.push_back(spec.sharedWithAnotherThread);

            if (static_cast<int32_t>(currentRow.Children().Size()) >= CHIPS_PER_ROW)
            {
                rows.Children().Append(currentRow);
                currentRow = controls::MakeRow(4.0);
            }
        }

        if (currentRow.Children().Size() > 0)
        {
            rows.Children().Append(currentRow);
        }

        // The grid goes straight into the card rather than into a scroll region of its own.
        //
        // A bounded grid with its own scrollbar was the first attempt and it was worse: the sidebar is
        // already a scroll region, so a pointer over the chips scrolled the chips and a pointer over
        // anything else scrolled the panel, and the reader could not predict which. The grid grows and
        // the sidebar scrolls it like any other part of the page.
        rows.Margin(ThicknessHelper::FromLengths(0.0, 4.0, 0.0, 0.0));

        // Capped in width so it matches the card's content column rather than the card's full width,
        // which would let the last chip in a row sit against the border.
        rows.HorizontalAlignment(HorizontalAlignment::Left);
        content.Children().Append(rows);

        if (topology.groupCount > 1)
        {
            // Said rather than hidden: on a machine with several processor groups the legacy affinity
            // API addresses one of them, and a user who cannot select a processor deserves to know why
            // rather than to conclude the application is broken.
            content.Children().Append(controls::MakeText(
                L"This machine has more than one processor group. This editor covers group 0.",
                SMALL_FONT_SIZE,
                true));
        }

        // --- Current and pending ----------------------------------------------
        m_affinityText = controls::MakeText(L"", SMALL_FONT_SIZE, true);
        m_affinityText.TextWrapping(TextWrapping::Wrap);
        m_affinityText.Margin(ThicknessHelper::FromLengths(0.0, 4.0, 0.0, 0.0));
        content.Children().Append(m_affinityText);

        m_topologyText = controls::MakeText(L"", SMALL_FONT_SIZE, true);
        m_topologyText.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(m_topologyText);

        StackPanel actions = controls::MakeRow(8.0);
        actions.Margin(ThicknessHelper::FromLengths(0.0, 4.0, 0.0, 0.0));
        actions.HorizontalAlignment(HorizontalAlignment::Right);

        m_affinityReset = controls::MakeButton(L"Reset", [this] {
            m_pendingAffinity.reset();
            _refreshChips();
            _setStatus(L"CPU selection reset to what the process currently has.", false);
        });
        m_affinityReset.Padding(ThicknessHelper::FromLengths(12.0, 5.0, 12.0, 5.0));
        actions.Children().Append(m_affinityReset);

        m_affinityApply = controls::MakeButton(L"Apply CPU selection", [this] { _applyAffinity(); });
        m_affinityApply.Padding(ThicknessHelper::FromLengths(12.0, 5.0, 12.0, 5.0));
        actions.Children().Append(m_affinityApply);

        content.Children().Append(actions);

        card.Child(content);
        host.Children().Append(card);
    }

    void ProcessDetailPanel::_buildAutomationCard(StackPanel const& host)
    {
        Border card = controls::MakeCard();

        StackPanel content = controls::MakeStack(6.0);

        // --- Process state -----------------------------------------------------
        TextBlock stateHeading = controls::MakeHeading(L"Process state", 14.0);
        content.Children().Append(stateHeading);

        m_suspendNote = controls::MakeText(SUSPEND_NOTE, SMALL_FONT_SIZE, true);
        m_suspendNote.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(m_suspendNote);

        StackPanel suspendRow = controls::MakeRow(8.0);
        m_suspendButton = controls::MakeButton(L"Suspend", [this] { _applySuspend(!m_suspended); });
        suspendRow.Children().Append(m_suspendButton);
        content.Children().Append(suspendRow);

        content.Children().Append(controls::MakeDivider());

        // --- Rule --------------------------------------------------------------
        TextBlock ruleHeading = controls::MakeHeading(L"Saved rule", 14.0);
        content.Children().Append(ruleHeading);

        m_ruleStatus = controls::MakeText(L"", SMALL_FONT_SIZE, true);
        m_ruleStatus.TextWrapping(TextWrapping::Wrap);
        content.Children().Append(m_ruleStatus);

        StackPanel ruleRow = controls::MakeRow(8.0);
        m_saveRule = controls::MakeButton(L"Save as rule", [this] { _saveRule(); });
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            m_saveRule,
            winrt::box_value(winrt::hstring{
                L"Remember these settings by executable name, and apply them to every future instance."}));
        ruleRow.Children().Append(m_saveRule);

        m_deleteRule = controls::MakeButton(L"Delete rule", [this] { _deleteRule(); });
        ruleRow.Children().Append(m_deleteRule);

        content.Children().Append(ruleRow);

        card.Child(content);
        host.Children().Append(card);
    }

    // ========================================================================
    // Selection
    // ========================================================================

    void ProcessDetailPanel::SetProcess(uint32_t pid, uint64_t createTime, std::string imageName)
    {
        if (pid == 0)
        {
            m_pid = 0;
            m_hasState = false;
            m_pendingAffinity.reset();
            m_details.Visibility(Visibility::Collapsed);
            m_placeholder.Visibility(Visibility::Visible);
            m_status.Visibility(Visibility::Collapsed);
            return;
        }

        // A different process, or the same identifier belonging to a different one. Clearing the
        // pending selection in the second case matters: a pending mask staged for the process that
        // has exited must not be applied to whatever inherited its identifier.
        if (m_pid != pid || m_createTime != createTime)
        {
            m_pendingAffinity.reset();
        }

        m_pid = pid;
        m_createTime = createTime;
        m_imageName = std::move(imageName);

        m_details.Visibility(Visibility::Visible);
        m_placeholder.Visibility(Visibility::Collapsed);

        _reload();
        _refreshRule();
    }

    void ProcessDetailPanel::Refresh(domain::ProcessView const& process)
    {
        if (process.identity.pid != m_pid)
        {
            return;
        }

        // Only the live figures are taken. The settings are not re-read: that opens the process, and
        // doing it on every frame would be a per-frame handle for a value that changes only when the
        // user changes it.
        m_cpuPercent = process.cpuPercent;
        m_workingSet = process.memory.workingSetSize;
        m_ratesUnavailable = process.ratesUnavailable;

        bool const wasSuspended = m_suspended;
        m_suspended = process.suspended;

        _refreshFigures();

        // The suspend button's label follows the process, not the click: a process suspended from
        // outside -- by another tool, or by a debugger -- must not leave this panel offering to
        // suspend it again.
        if (wasSuspended != m_suspended)
        {
            m_suspendButton.Content(winrt::box_value(winrt::hstring{m_suspended ? L"Resume" : L"Suspend"}));
        }
    }

    // ========================================================================
    // Presentation
    // ========================================================================

    void ProcessDetailPanel::_reload()
    {
        diagnostics::ScopedTimer const whole{"sidebar: reload"};

        {
            diagnostics::ScopedTimer const timer{"sidebar: read settings"};
            m_state = m_coordinator.ReadProcessTuning(m_pid);
        }

        m_hasState = m_state.AnyAvailable();
        m_pendingAffinity.reset();

        if (m_state.suspendState == platform::ProcessSuspendState::Suspended)
        {
            m_suspended = true;
        }
        else if (m_state.suspendState == platform::ProcessSuspendState::Running)
        {
            m_suspended = false;
        }

        {
            diagnostics::ScopedTimer const timer{"sidebar: read path"};
            m_path.Text(winrt::hstring{_toWide(m_coordinator.ProcessExecutablePath(m_pid))});
        }
        if (m_path.Text().size() == 0)
        {
            m_path.Text(L"The executable path is not available for this process.");
        }

        // The state push is timed as one stage: it changes a handful of properties, and if it overruns
        // then what is expensive is the layout it triggers -- revealing the panel for the first time
        // means WinUI has to realise four combo boxes and their item templates.
        {
            diagnostics::ScopedTimer const timer{"sidebar: push state"};
            _applyStateToControls();
            _refreshChips();
            _refreshFigures();
            _refreshUndo();
        }

        if (!m_hasState)
        {
            _setStatus(L"This process does not report any adjustable settings. That is normal for a "
                       L"protected system process.",
                       true);
            return;
        }

        // Which settings are unavailable is stated once, as a list, rather than beside each row: the
        // rows are all present, and a row-level explanation repeated four times is noise.
        std::wstring unavailable;
        auto note = [&unavailable](wchar_t const* name) {
            if (!unavailable.empty())
            {
                unavailable += L", ";
            }
            unavailable += name;
        };
        if (!m_state.cpuPriorityAvailable)
        {
            note(L"CPU priority");
        }
        if (!m_state.memoryPriorityAvailable)
        {
            note(L"memory priority");
        }
        if (!m_state.ioPriorityAvailable)
        {
            note(L"I/O priority");
        }
        if (!m_state.efficiencyModeAvailable)
        {
            note(L"efficiency mode");
        }
        if (!m_state.affinityAvailable)
        {
            note(L"CPU affinity");
        }

        if (!unavailable.empty())
        {
            _setStatus(std::wstring{L"Not readable for this process: "} + unavailable + L".", true);
        }
        else
        {
            m_status.Visibility(Visibility::Collapsed);
        }
    }

    void ProcessDetailPanel::_applyStateToControls()
    {
        auto selectByValue = [](ComboBox const& box, size_t count, auto value, auto table) {
            for (size_t i = 0; i < count; ++i)
            {
                if (table[i].value == value)
                {
                    box.SelectedIndex(static_cast<int32_t>(i));
                    return;
                }
            }
            // No matching entry: the process reports a value this build does not offer, which happens
            // for realtime (deliberately absent from the table). The selection is cleared rather than
            // left on an unrelated row, so the user is not shown a value the process does not have.
            box.SelectedIndex(-1);
        };

        selectByValue(m_cpuBox, std::size(CPU_OPTIONS), m_state.cpuPriority, CPU_OPTIONS);
        selectByValue(m_memoryBox, std::size(MEMORY_OPTIONS), m_state.memoryPriority, MEMORY_OPTIONS);
        selectByValue(m_ioBox, std::size(IO_OPTIONS), m_state.ioPriority, IO_OPTIONS);
        selectByValue(m_efficiencyBox, std::size(EFFICIENCY_OPTIONS), m_state.efficiencyMode, EFFICIENCY_OPTIONS);

        // A control for a setting that could not be read is disabled rather than hidden: the row is
        // part of the layout, and removing it would move everything below it as the selection changes.
        m_cpuBox.IsEnabled(m_state.cpuPriorityAvailable);
        m_cpuApply.IsEnabled(m_state.cpuPriorityAvailable);

        m_memoryBox.IsEnabled(m_state.memoryPriorityAvailable);
        m_memoryApply.IsEnabled(m_state.memoryPriorityAvailable);

        m_ioBox.IsEnabled(m_state.ioPriorityAvailable);
        m_ioApply.IsEnabled(m_state.ioPriorityAvailable);

        m_efficiencyBox.IsEnabled(m_state.efficiencyModeAvailable);
        m_efficiencyApply.IsEnabled(m_state.efficiencyModeAvailable);

        m_affinityApply.IsEnabled(m_state.affinityAvailable);
        m_affinityReset.IsEnabled(m_state.affinityAvailable);

        m_suspendButton.Content(winrt::box_value(winrt::hstring{m_suspended ? L"Resume" : L"Suspend"}));
        m_suspendButton.IsEnabled(m_state.suspendState != platform::ProcessSuspendState::Unknown);
    }

    void ProcessDetailPanel::_refreshChips()
    {
        uint64_t const mask = m_pendingAffinity.has_value() ? m_pendingAffinity.value() : m_state.affinityMask;
        bool const editable = m_state.affinityAvailable;

        for (CoreChip& chip : m_chips)
        {
            bool const selected = (_maskBit(chip.logicalIndex) & mask) != 0;

            chip.element.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                selected ? CHIP_SELECTED_FILL : CHIP_FILL));
            chip.label.Foreground(selected
                                      ? winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                                            winrt::Windows::UI::Colors::White())
                                      : controls::ThemedBrush(theme::SUBTLE_TEXT));
            chip.element.Opacity(editable ? 1.0 : 0.4);
        }

        uint32_t const groupSize =
            m_state.affinityGroupSize > 0 ? m_state.affinityGroupSize : m_groupSize;

        std::wstring text = L"Current: " + _formatMask(m_state.affinityMask, groupSize);
        if (m_pendingAffinity.has_value() && m_pendingAffinity.value() != m_state.affinityMask)
        {
            text += L"\nSelected: " + _formatMask(m_pendingAffinity.value(), groupSize) + L" (not applied yet)";
        }
        m_affinityText.Text(text);

        platform::ProcessorTopology const& topology = m_coordinator.ProcessorTopology();
        std::wstring topologyText = L"Detected: " + std::to_wstring(topology.logicalProcessors.size()) +
                                    L" logical processors on " + std::to_wstring(topology.physicalCoreCount) +
                                    L" physical cores";
        if (topology.hybrid)
        {
            topologyText += L" \u00B7 performance and efficiency cores";
        }
        else if (m_chipIsSmtSibling.size() > 0 && std::any_of(m_chipIsSmtSibling.begin(), m_chipIsSmtSibling.end(), [](bool v) { return v; }))
        {
            topologyText += L" \u00B7 simultaneous multithreading active";
        }
        topologyText += L" \u00B7 this process may use " +
                        std::to_wstring(static_cast<uint32_t>(std::popcount(m_state.affinityMask))) +
                        L" of them";
        m_topologyText.Text(topologyText);
    }

    void ProcessDetailPanel::_refreshFigures()
    {
        std::wstring figures = L"PID " + std::to_wstring(m_pid);
        figures += L"  \u00B7  CPU ";
        figures += m_ratesUnavailable ? L"\u2014" : _toWide(FormatProcessCpuPercent(m_cpuPercent));
        figures += L"  \u00B7  Memory " + _toWide(FormatBytes(m_workingSet));

        if (m_suspended)
        {
            figures += L"  \u00B7  Suspended";
        }

        m_figures.Text(figures);

        m_title.Text(winrt::hstring{_toWide(ProcessDisplayName(m_pid, m_imageName))});
    }

    void ProcessDetailPanel::_refreshUndo()
    {
        size_t const depth = _undoDepth();
        if (depth == 0)
        {
            m_undoButton.IsEnabled(false);
            m_undoButton.Content(winrt::box_value(winrt::hstring{L"Undo last change"}));
            return;
        }

        m_undoButton.IsEnabled(true);
        m_undoButton.Content(winrt::box_value(
            winrt::hstring{L"Undo last change (" + std::to_wstring(depth) + L" step" +
                           (depth == 1 ? L"" : L"s") + L" available)"}));
    }

    void ProcessDetailPanel::_refreshRule()
    {
        m_ruleStatus.Text(L"No rule is saved for this executable.");
        m_deleteRule.IsEnabled(false);
        m_saveRule.IsEnabled(!m_imageName.empty() && m_hasState);

        if (m_imageName.empty())
        {
            m_saveRule.IsEnabled(false);
            return;
        }

        std::string const loweredName = _lowered(m_imageName);

        for (core::ProcessRule const& rule : m_coordinator.ProcessRules())
        {
            if (_lowered(rule.executableName) != loweredName)
            {
                continue;
            }

            std::wstring text = rule.enabled ? L"Applies to every new " : L"Saved but disabled for ";
            text += _toWide(m_imageName) + L": " + _toWide(rule.Describe()) + L".";
            m_ruleStatus.Text(text);
            m_deleteRule.IsEnabled(true);
            return;
        }
    }

    void ProcessDetailPanel::_setStatus(std::wstring text, bool isFailure)
    {
        if (text.empty())
        {
            m_status.Visibility(Visibility::Collapsed);
            return;
        }

        m_status.Text(text);

        // The warning colour rather than the error one: everything the panel can refuse is a
        // permission or an unsupported setting, and both are conditions rather than faults. The
        // theme's own warning key is used so this follows light and dark without a second definition.
        m_status.Foreground(controls::ThemedBrush(isFailure ? L"SystemFillColorCautionBrush"
                                                            : L"TextFillColorSecondaryBrush"));
        m_status.Visibility(Visibility::Visible);
    }

    // ========================================================================
    // Actions
    // ========================================================================

    void ProcessDetailPanel::_send(platform::ProcessTuningChange const& change, std::wstring const& action)
    {
        if (m_pid == 0 || change.Empty())
        {
            return;
        }

        // Recorded before the attempt, because the values it captures are what the undo has to
        // restore and they are only readable now.
        platform::ProcessTuningState const before = m_state;

        platform::ProcessActionResult const result = m_coordinator.ApplyProcessTuning(m_pid, change);

        if (result.applied > 0)
        {
            _rememberUndo(change, before);
        }

        if (result.Succeeded())
        {
            _setStatus(action + L" applied.", false);
        }
        else
        {
            std::wstring message = action + L" failed: " + _toWide(result.message);
            if (result.PartiallyApplied())
            {
                // Stated explicitly, because a partial application leaves the process in a state the
                // user did not ask for and a bare "failed" would not say so.
                message = action + L" partly applied \u2014 " + std::to_wstring(result.applied) + L" of " +
                          std::to_wstring(result.requested) + L" settings took effect. " +
                          _toWide(result.message);
            }
            _setStatus(message, true);
        }

        // Re-read so the panel shows what the process actually has rather than what was asked for.
        // The two differ whenever a setting is clamped or refused, and showing the request would make
        // the panel claim a state the process is not in.
        _reload();
        _refreshRule();
    }

    void ProcessDetailPanel::_applyCpuPriority()
    {
        int32_t const index = m_cpuBox.SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= std::size(CPU_OPTIONS))
        {
            return;
        }

        platform::ProcessTuningChange change;
        change.cpuPriority = CPU_OPTIONS[index].value;
        _send(change, L"CPU priority");
    }

    void ProcessDetailPanel::_applyMemoryPriority()
    {
        int32_t const index = m_memoryBox.SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= std::size(MEMORY_OPTIONS))
        {
            return;
        }

        platform::ProcessTuningChange change;
        change.memoryPriority = MEMORY_OPTIONS[index].value;
        _send(change, L"Memory priority");
    }

    void ProcessDetailPanel::_applyIoPriority()
    {
        int32_t const index = m_ioBox.SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= std::size(IO_OPTIONS))
        {
            return;
        }

        platform::ProcessTuningChange change;
        change.ioPriority = IO_OPTIONS[index].value;
        _send(change, L"I/O priority");
    }

    void ProcessDetailPanel::_applyEfficiencyMode()
    {
        int32_t const index = m_efficiencyBox.SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= std::size(EFFICIENCY_OPTIONS))
        {
            return;
        }

        platform::ProcessTuningChange change;
        change.efficiencyMode = EFFICIENCY_OPTIONS[index].value;
        _send(change, L"Efficiency mode");
    }

    void ProcessDetailPanel::_applyAffinity()
    {
        if (!m_pendingAffinity.has_value())
        {
            _setStatus(L"Choose at least one processor before applying.", true);
            return;
        }

        if (m_pendingAffinity.value() == 0)
        {
            // An empty mask would leave the process with nowhere to run, and Windows refuses it with
            // an invalid-parameter error that says nothing about why.
            _setStatus(L"At least one processor must stay selected.", true);
            return;
        }

        if (m_pendingAffinity.value() == m_state.affinityMask)
        {
            _setStatus(L"The CPU selection is already what the process has.", false);
            m_pendingAffinity.reset();
            _refreshChips();
            return;
        }

        platform::ProcessTuningChange change;
        change.affinityMask = m_pendingAffinity.value();
        _send(change, L"CPU affinity");
    }

    void ProcessDetailPanel::_applySuspend(bool suspend)
    {
        platform::ProcessTuningChange change;
        change.suspend = suspend;

        // Suspending is the one change here that can lose work, so it is confirmed. Resuming is not:
        // it restores a state the user already chose, and a confirmation on the way back would be an
        // obstacle to undoing a mistake.
        if (!suspend)
        {
            _send(change, L"Resume");
            return;
        }

        winrt::Microsoft::UI::Xaml::Controls::ContentDialog dialog;
        dialog.XamlRoot(m_root.XamlRoot());
        dialog.Title(winrt::box_value(winrt::hstring{L"Suspend this process?"}));
        dialog.Content(winrt::box_value(winrt::hstring{
            winrt::hstring{L"Every thread of "} + winrt::hstring{_toWide(m_imageName)} +
            L" will stop until it is resumed. A process holding unsaved work can be lost if it never "
            L"resumes, and some drivers cannot recover from a suspension at all."}));
        dialog.PrimaryButtonText(L"Suspend");
        dialog.CloseButtonText(L"Cancel");
        dialog.DefaultButton(winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton::Close);

        dialog.PrimaryButtonClick(
            [this](winrt::Microsoft::UI::Xaml::Controls::ContentDialog const&,
                   winrt::Microsoft::UI::Xaml::Controls::ContentDialogButtonClickEventArgs const&) {
                platform::ProcessTuningChange suspendChange;
                suspendChange.suspend = true;
                _send(suspendChange, L"Suspend");
            });

        dialog.ShowAsync();
    }

    void ProcessDetailPanel::_applyPreset(size_t presetIndex)
    {
        if (presetIndex >= std::size(PRESETS))
        {
            return;
        }

        platform::ProcessorTopology const& topology = m_coordinator.ProcessorTopology();
        uint64_t const current =
            m_pendingAffinity.has_value() ? m_pendingAffinity.value() : m_state.affinityMask;

        uint64_t mask = 0;
        switch (PRESETS[presetIndex].preset)
        {
            case AffinityPreset::All:
                mask = (m_groupSize >= 64) ? ~uint64_t{0} : ((uint64_t{1} << m_groupSize) - 1);
                break;

            case AffinityPreset::None:
                mask = 0;
                break;

            case AffinityPreset::Invert:
                mask = ~current;
                break;

            case AffinityPreset::PrimaryThreads:
                for (platform::ProcessorCore const& core : topology.logicalProcessors)
                {
                    if (core.group == 0 && core.primaryThread)
                    {
                        mask |= _maskBit(core.logicalIndex);
                    }
                }
                break;

            case AffinityPreset::PerformanceCores:
                for (platform::ProcessorCore const& core : topology.logicalProcessors)
                {
                    if (core.group == 0 && core.efficiencyClass >= m_performanceClass)
                    {
                        mask |= _maskBit(core.logicalIndex);
                    }
                }
                break;

            case AffinityPreset::EfficiencyCores:
                for (platform::ProcessorCore const& core : topology.logicalProcessors)
                {
                    if (core.group == 0 && core.efficiencyClass < m_performanceClass)
                    {
                        mask |= _maskBit(core.logicalIndex);
                    }
                }
                break;
        }

        // Bits outside the group are cleared rather than kept: a mask naming a processor the machine
        // does not have in this group is refused by the kernel, and the refusal reads as an invalid
        // parameter rather than as anything the user can act on.
        if (m_groupSize < 64)
        {
            mask &= (uint64_t{1} << m_groupSize) - 1;
        }

        m_pendingAffinity = mask;
        _refreshChips();
        _setStatus(L"Selection staged. Press Apply CPU selection to send it.", false);
    }

    // ========================================================================
    // Rules
    // ========================================================================

    void ProcessDetailPanel::_saveRule()
    {
        if (m_imageName.empty() || !m_hasState)
        {
            return;
        }

        // The rule records what the process currently has, not what is selected in the controls: a
        // rule that stored an unapplied choice would silently apply something the user never sent.
        platform::ProcessTuningChange settings;
        if (m_state.cpuPriorityAvailable)
        {
            settings.cpuPriority = m_state.cpuPriority;
        }
        if (m_state.memoryPriorityAvailable)
        {
            settings.memoryPriority = m_state.memoryPriority;
        }
        if (m_state.ioPriorityAvailable)
        {
            settings.ioPriority = m_state.ioPriority;
        }
        if (m_state.efficiencyModeAvailable)
        {
            settings.efficiencyMode = m_state.efficiencyMode;
        }

        // Affinity is stored only when the process is actually restricted. A rule that pinned every
        // processor would break on a machine with a different core count, which is exactly the case a
        // rule is meant to survive.
        if (m_state.affinityAvailable && !m_state.AffinityIsUnrestricted())
        {
            settings.affinityMask = m_state.affinityMask;
        }

        if (settings.Empty())
        {
            _setStatus(L"There is nothing to save: none of this process's settings is readable.", true);
            return;
        }

        core::ProcessRule rule;
        rule.executableName = m_imageName;
        rule.settings = settings;
        rule.enabled = true;

        std::vector<core::ProcessRule> rules = m_coordinator.ProcessRules();

        // Replaced rather than appended when one already names this executable: two rules for one name
        // would make the applied one depend on the order they happened to be created in.
        std::string const loweredName = _lowered(m_imageName);

        rules.erase(std::remove_if(rules.begin(),
                                   rules.end(),
                                   [&loweredName](core::ProcessRule const& existing) {
                                       return _lowered(existing.executableName) == loweredName;
                                   }),
                    rules.end());

        rules.push_back(std::move(rule));

        // Saved through the coordinator rather than to a file here: the application owns the file, and
        // the sampler is the thing that has to see the new rule.
        m_coordinator.SetProcessRules(std::move(rules));

        _refreshRule();
        _setStatus(L"Rule saved. It is applied the next time this executable starts.", false);
    }

    void ProcessDetailPanel::_deleteRule()
    {
        if (m_imageName.empty())
        {
            return;
        }

        std::string const loweredName = _lowered(m_imageName);

        std::vector<core::ProcessRule> rules = m_coordinator.ProcessRules();
        auto const removed = std::remove_if(rules.begin(),
                                            rules.end(),
                                            [&loweredName](core::ProcessRule const& existing) {
                                                return _lowered(existing.executableName) == loweredName;
                                            });

        if (removed == rules.end())
        {
            return;
        }

        rules.erase(removed, rules.end());
        m_coordinator.SetProcessRules(std::move(rules));

        _refreshRule();
        _setStatus(L"Rule deleted.", false);
    }

    // ========================================================================
    // Undo
    // ========================================================================

    void ProcessDetailPanel::_rememberUndo(platform::ProcessTuningChange const& change,
                                           platform::ProcessTuningState const& before)
    {
        uint64_t const key = _identityKeyOf(m_pid, m_createTime);

        if (m_undo.find(key) == m_undo.end())
        {
            m_undoOrder.push_back(key);

            // The oldest process's history is dropped once too many have been touched. A history that
            // grew with the number of processes the user ever selected would be a leak with no visible
            // benefit: nothing older than the last few processes is ever reachable again.
            while (m_undoOrder.size() > MAX_TRACKED_PROCESSES)
            {
                m_undo.erase(m_undoOrder.front());
                m_undoOrder.pop_front();
            }
        }

        std::deque<UndoEntry>& stack = m_undo[key];
        stack.push_back(UndoEntry{change, before});
        while (stack.size() > MAX_UNDO_PER_PROCESS)
        {
            stack.pop_front();
        }
    }

    std::optional<ProcessDetailPanel::UndoEntry> ProcessDetailPanel::_popUndo() const
    {
        uint64_t const key = _identityKeyOf(m_pid, m_createTime);
        auto const found = m_undo.find(key);
        if (found == m_undo.end() || found->second.empty())
        {
            return std::nullopt;
        }

        UndoEntry entry = found->second.back();
        found->second.pop_back();
        return entry;
    }

    size_t ProcessDetailPanel::_undoDepth() const
    {
        uint64_t const key = _identityKeyOf(m_pid, m_createTime);
        auto const found = m_undo.find(key);
        return found == m_undo.end() ? 0 : found->second.size();
    }

    platform::ProcessTuningChange ProcessDetailPanel::_inverseOf(UndoEntry const& entry) const
    {
        // Only the settings the recorded change touched are restored. Restoring the whole recorded
        // state would also reset anything the process changed on its own in the meantime, which is a
        // change the user did not ask for.
        platform::ProcessTuningChange inverse;

        if (entry.change.cpuPriority.has_value() && entry.before.cpuPriorityAvailable)
        {
            inverse.cpuPriority = entry.before.cpuPriority;
        }
        if (entry.change.memoryPriority.has_value() && entry.before.memoryPriorityAvailable)
        {
            inverse.memoryPriority = entry.before.memoryPriority;
        }
        if (entry.change.ioPriority.has_value() && entry.before.ioPriorityAvailable)
        {
            inverse.ioPriority = entry.before.ioPriority;
        }
        if (entry.change.efficiencyMode.has_value() && entry.before.efficiencyModeAvailable)
        {
            inverse.efficiencyMode = entry.before.efficiencyMode;
        }
        if (entry.change.affinityMask.has_value() && entry.before.affinityAvailable)
        {
            inverse.affinityMask = entry.before.affinityMask;
        }
        if (entry.change.suspend.has_value())
        {
            // The reverse of the suspension that was requested, not of the state that was recorded:
            // if a suspend was asked for and succeeded, resuming is the inverse whether or not the
            // snapshot had caught up with it.
            inverse.suspend = !entry.change.suspend.value();
        }

        return inverse;
    }

    void ProcessDetailPanel::_undoLastChange()
    {
        std::optional<UndoEntry> const entry = _popUndo();
        if (!entry.has_value())
        {
            _refreshUndo();
            return;
        }

        platform::ProcessTuningChange const inverse = _inverseOf(entry.value());
        if (inverse.Empty())
        {
            // Nothing about the recorded change can be put back -- the settings it altered are no
            // longer readable. Saying so is better than a button that appears to do nothing.
            _setStatus(L"Nothing could be restored: the settings that change altered are not readable now.", true);
            _refreshUndo();
            return;
        }

        platform::ProcessActionResult const result = m_coordinator.ApplyProcessTuning(m_pid, inverse);

        // Deliberately not recorded as a new undo step. An undo that itself became undoable would let
        // the button walk in circles, and the previous state is already what the stack describes.
        if (result.Succeeded())
        {
            _setStatus(L"The last change was undone.", false);
        }
        else
        {
            _setStatus(L"The change could not be undone: " + _toWide(result.message), true);
        }

        _reload();
        _refreshRule();
    }
}
