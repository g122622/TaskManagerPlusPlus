// The process detail sidebar: everything that can be adjusted about one process.
//
// The process list answers "what is running"; this answers "what is this one doing, and how is the
// operating system treating it". It is a separate control from the page rather than another region
// inside it because the page is already the largest file in the UI layer, and because the sidebar
// owns a genuinely separate concern: the list reads published counters and never touches a handle,
// while this opens one per interaction.
//
// What it offers, and why each is here rather than in the list's context menu:
//
//   * Scheduling, memory, I/O and power. Four independent settings that the operating system
//     exposes through four different APIs, each with its own set of legal values. They belong
//     together because they answer the same question -- how much of the machine may this process
//     have -- and a menu cannot show what a value currently is.
//   * CPU affinity, as a grid of the machine's actual logical processors. A mask in hexadecimal is
//     unreadable, and the question a user has is "which cores", which is a picture rather than a
//     number.
//   * Suspension. The one control here that stops a process without ending it.
//   * A saved rule, so the same settings can be applied automatically the next time the executable
//     starts. Without it every setting here is lost the moment the process exits, which is the
//     single biggest limitation of a per-process control.
//
// Two behaviours are intentional and worth stating:
//
//   1. Nothing is applied as the user picks it. A combo box changes a staging value; a button
//      applies it. Changing a process's priority is a real change to a running system, and the
//      Windows task manager asks for a confirm click for the same reason.
//   2. Every applied change is recorded, and the last few can be undone. A priority set by mistake
//      is otherwise unrecoverable without knowing what the value was before.
#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "Domain/ProcessModel.h"
#include "Platform/ProcessTuningTypes.h"

namespace tmpp::ui
{
    /**
     * @brief The sidebar that shows and adjusts one process.
     */
    class ProcessDetailPanel
    {
    public:
        explicit ProcessDetailPanel(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::ScrollViewer Root() const { return m_root; }

        /**
         * @brief Shows one process, or clears the panel when @p pid is zero.
         *
         * Reads the process's settings immediately, which opens it. That is the only cost on this
         * path and it happens once per selection rather than per frame.
         *
         * @param pid Process to show.
         * @param createTime The process's creation time, so state that outlives a selection -- the
         *        undo history in particular -- can be told apart from a process that has since
         *        inherited the same identifier.
         * @param imageName The process's image name, which is what a saved rule is keyed on.
         */
        void SetProcess(uint32_t pid, uint64_t createTime, std::string imageName);

        /**
         * @brief Updates the live figures for the process already being shown.
         *
         * Called from the page's refresh loop. It deliberately does not re-read the settings: doing
         * so would open the process every frame, and the settings do not change on their own. What
         * changes on its own is the CPU and memory the process is using, and whether it is still
         * running.
         *
         * @param process The current sample's row for this process. Must outlive the call; nothing
         *        is retained.
         */
        void Refresh(domain::ProcessView const& process);

        /// True when the panel is showing a process, so the page knows whether to draw the
        /// placeholder or the sidebar behind a collapsed pane.
        [[nodiscard]] bool HasSelection() const noexcept { return m_pid != 0; }

    private:
        // ====================================================================
        // Undo history
        // ====================================================================

        /**
         * @brief One applied change, and what the settings were before it.
         *
         * The change is recorded rather than the whole state, so an undo restores exactly the
         * settings that were altered and leaves everything else -- including a priority the process
         * changed itself in the meantime -- alone.
         */
        struct UndoEntry
        {
            platform::ProcessTuningChange change;
            platform::ProcessTuningState before;
        };

        /// Deepest history kept for one process. Deep enough to walk back through a few experiments
        /// and shallow enough that the whole record is a few hundred bytes per process.
        static constexpr size_t MAX_UNDO_PER_PROCESS = 8;

        /// How many processes keep a history at all. See @c _rememberUndo for the policy.
        static constexpr size_t MAX_TRACKED_PROCESSES = 16;

        void _rememberUndo(platform::ProcessTuningChange const& change, platform::ProcessTuningState const& before);
        [[nodiscard]] std::optional<UndoEntry> _popUndo() const;
        [[nodiscard]] size_t _undoDepth() const;

        /// Builds the change that restores the settings an entry recorded.
        [[nodiscard]] platform::ProcessTuningChange _inverseOf(UndoEntry const& entry) const;

        // ====================================================================
        // Construction
        // ====================================================================

        void _buildLayout();

        /// Builds the process summary card.
        void _buildSummaryCard(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& host);

        /// Builds the four priority rows.
        void _buildPriorityCard(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& host);

        /// Builds the affinity editor, including one chip per logical processor.
        void _buildAffinityCard(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& host);

        /// Builds the rule and suspend card.
        void _buildAutomationCard(winrt::Microsoft::UI::Xaml::Controls::StackPanel const& host);

        // ====================================================================
        // Presentation
        // ====================================================================

        /// Reads the process's settings and writes them into the controls.
        void _reload();

        /// Writes the cached settings into the controls without reading anything.
        void _applyStateToControls();

        /// Updates the chip highlights from the pending mask.
        void _refreshChips();

        /// Updates the summary card's live figures.
        void _refreshFigures();

        /// Updates the undo button's label and enabled state.
        void _refreshUndo();

        /// Updates the rule card from the coordinator's current rule set.
        void _refreshRule();

        /// Shows one line of feedback at the top of the panel.
        void _setStatus(std::wstring text, bool isFailure);

        // ====================================================================
        // Actions
        // ====================================================================

        /// Reads the combo boxes and sends the one setting that changed.
        void _applyCpuPriority();

        void _applyMemoryPriority();

        void _applyIoPriority();

        void _applyEfficiencyMode();

        /// Sends the pending affinity mask.
        void _applyAffinity();

        /// Sends a suspend or resume request.
        void _applySuspend(bool suspend);

        /// Pops one entry from the history and sends the change that reverses it.
        void _undoLastChange();

        /// Saves the process's current settings as a rule, replacing any rule with the same name.
        void _saveRule();

        /// Removes the rule that matches the process being shown.
        void _deleteRule();

        /// Applies a preset to the pending mask: every processor, none, the inverse, one thread per
        /// physical core, or just the performance or just the efficiency cores.
        void _applyPreset(size_t presetIndex);

        /// Sends a change and handles the result: status line, history, and a reload.
        void _send(platform::ProcessTuningChange const& change, std::wstring const& action);

        // ====================================================================
        // State
        // ====================================================================

        core::SamplingCoordinator& m_coordinator;

        uint32_t m_pid{0};
        uint64_t m_createTime{0};
        std::string m_imageName;

        /// The settings as last read. Meaningless when @c m_hasState is false.
        platform::ProcessTuningState m_state;
        bool m_hasState{false};

        /// Live figures for the summary line, kept because the status line and the undo button are
        /// updated on paths that do not have the snapshot in hand.
        double m_cpuPercent{0.0};
        uint64_t m_workingSet{0};
        bool m_ratesUnavailable{true};
        bool m_suspended{false};

        /// The mask the chips describe, or empty when the user has not touched them. Empty means
        /// "show the process's own affinity", which is what lets the panel reload without discarding
        /// an edit only when there is one.
        std::optional<uint64_t> m_pendingAffinity;

        /// True for each chip whose physical core is shared with another logical processor, so the
        /// caption can say whether multithreading is in play without walking the topology again.
        std::vector<bool> m_chipIsSmtSibling;

        /// The logical processors in the affinity group, so a chip index maps to a bit position.
        uint32_t m_groupSize{64};

        /// Whether the machine labels more than one efficiency class, so the core naming and the
        /// class presets are only offered where they mean something.
        bool m_hybrid{false};

        /// The efficiency class that identifies a performance core: the highest present.
        ///
        /// Higher rather than lower, which is the opposite of what the field's name suggests. See
        /// ProcessorCore::efficiencyClass for the evidence.
        uint8_t m_performanceClass{0};

        // Undo history, keyed by process identity, with the insertion order kept so the oldest
        // process's history can be dropped when too many have been touched.
        mutable std::unordered_map<uint64_t, std::deque<UndoEntry>> m_undo;
        mutable std::deque<uint64_t> m_undoOrder;

        // --- Controls ---------------------------------------------------------

        winrt::Microsoft::UI::Xaml::Controls::ScrollViewer m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_content{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_placeholder{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_status{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_details{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_title{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_path{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_figures{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_stateNote{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_cpuBox{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_memoryBox{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_ioBox{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::ComboBox m_efficiencyBox{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::Button m_cpuApply{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_memoryApply{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_ioApply{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_efficiencyApply{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::Button m_undoButton{nullptr};

        /// One toggle chip per logical processor, in chip order.
        struct CoreChip
        {
            uint32_t logicalIndex{0};
            winrt::Microsoft::UI::Xaml::Controls::Border element{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock label{nullptr};
        };

        std::vector<CoreChip> m_chips;

        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_affinityText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_topologyText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_affinityApply{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_affinityReset{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_ruleStatus{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_saveRule{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Button m_deleteRule{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::Button m_suspendButton{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_suspendNote{nullptr};
    };
}
