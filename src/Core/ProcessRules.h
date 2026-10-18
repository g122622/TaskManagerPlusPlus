// Rules that apply a set of process settings automatically, by executable name.
//
// The sidebar can change one process's scheduling, memory and power settings, but a process that is
// restarted -- which is what happens every time a game or a build tool is launched -- comes back with
// the operating system's defaults. A rule records a set of settings against an executable name so
// that the next instance of it is adjusted without the user having to find it in the list again.
//
// Two pieces, deliberately separate:
//
//   * ProcessRuleStore reads and writes the file. It knows nothing about processes.
//   * ProcessRuleEngine decides which rule applies to which process and applies it. It knows nothing
//     about files. The applier is injected rather than reached for, so the matching can be tested
//     without a machine to run it on.
//
// The engine runs on the sampling thread, because that is where a newly appeared process is first
// observed and because opening a handle per new process on the UI thread would put an unbounded cost
// on a frame. Everything it exposes to the UI is guarded, and what it did is reported through a queue
// the UI drains rather than through a callback into the UI from the sampler thread.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "Domain/ProcessModel.h"
#include "Platform/ProcessActionTypes.h"
#include "Platform/ProcessTuningTypes.h"
#include "Platform/Result.h"

namespace tmpp::core
{
    /**
     * @brief A saved set of settings, keyed on an executable's name.
     *
     * The image name rather than the full path: a rule is meant to survive an application being
     * updated, reinstalled or moved, and the path changes in all three cases while the name does not.
     * It is matched case-insensitively, because Windows file names are.
     */
    struct ProcessRule
    {
        /// The executable's image name, e.g. "game.exe". Matched without regard to case.
        std::string executableName;

        /// What to apply. An unset member is left at whatever the process already has, which is what
        /// lets a rule say only "efficiency mode off" without also pinning a priority.
        platform::ProcessTuningChange settings;

        /// Whether the rule takes part in matching. Kept so a rule can be parked without being
        /// deleted, which is what a user does when they are not sure a rule is the cause of something.
        bool enabled{true};

        [[nodiscard]] bool Valid() const noexcept
        {
            return !executableName.empty() && !settings.Empty();
        }

        /// A one-line description of what the rule does, for the UI to list.
        [[nodiscard]] std::string Describe() const;
    };

    /**
     * @brief What the engine did to one process, for the UI to report.
     */
    struct ProcessRuleEvent
    {
        uint32_t pid{0};
        std::string executableName;
        platform::ProcessActionResult result;

        /// True when the rule was found but its application failed, so the UI can say so without
        /// treating an ordinary success as news.
        [[nodiscard]] bool Failed() const noexcept { return !result.Succeeded(); }
    };

    /**
     * @brief Reads and writes the rule file.
     *
     * Not thread-safe; the owner serialises access. A missing or damaged file yields no rules rather
     * than an error: rules are an accelerator, and a damaged file must not stop the application.
     */
    class ProcessRuleStore
    {
    public:
        explicit ProcessRuleStore(std::string path) : m_path(std::move(path)) {}

        [[nodiscard]] std::vector<ProcessRule> Load() const;
        [[nodiscard]] VoidResult Save(std::vector<ProcessRule> const& rules) const;

        [[nodiscard]] std::string const& FilePath() const noexcept { return m_path; }

    private:
        std::string m_path;
    };

    /**
     * @brief Applies matching rules to processes as they are observed.
     */
    class ProcessRuleEngine
    {
    public:
        /// Applies one change to one process. Injected so the engine does not need a Windows API to
        /// be tested, and so the coordinator stays the only thing that owns the tuning module.
        using Applier = std::function<platform::ProcessActionResult(uint32_t, uint64_t, platform::ProcessTuningChange const&)>;

        ProcessRuleEngine() = default;

        /**
         * @brief Replaces the rule set.
         *
         * The record of what has already been applied is cleared, so a changed rule takes effect on
         * the next sample rather than only on the next launch of the process it names. Not clearing it
         * would make editing a rule appear to do nothing.
         */
        void SetRules(std::vector<ProcessRule> rules);

        [[nodiscard]] std::vector<ProcessRule> Rules() const;

        /// True when at least one enabled rule exists, so a caller can skip the per-sample work
        /// entirely on a machine where the feature has never been used.
        [[nodiscard]] bool HasActiveRules() const;

        /**
         * @brief Applies any matching rule to each process it has not been applied to yet.
         *
         * Called once per published sample, on the sampling thread. A process is remembered by its
         * identity -- pid plus creation time -- so a rule is applied exactly once per process, and a
         * process that is restarted is treated as a new one, which is the whole point of the feature.
         *
         * @param snapshot The sample just published.
         * @param apply How to apply a change.
         */
        void Evaluate(domain::ProcessSnapshotView const& snapshot, Applier const& apply);

        /**
         * @brief Takes the queued record of what was applied.
         *
         * Drained rather than pushed, so the sampler thread never calls into the UI. An empty result
         * is the ordinary case: most samples observe no new process that a rule matches.
         */
        [[nodiscard]] std::vector<ProcessRuleEvent> TakeEvents();

        /// How many times a rule has been applied since the engine was created, for diagnostics.
        [[nodiscard]] uint64_t AppliedCount() const;

    private:
        /// Identity of a process that has been dealt with, as pid and creation time packed together.
        [[nodiscard]] static uint64_t _identityKey(platform::ProcessIdentity const& identity) noexcept;

        mutable std::mutex m_mutex;

        std::vector<ProcessRule> m_rules;

        /// Identities already handled. Rebuilt from each snapshot rather than accumulated, so it
        /// cannot grow without bound on a machine that starts a process every second.
        std::vector<uint64_t> m_handled;

        std::vector<ProcessRuleEvent> m_events;

        uint64_t m_appliedCount{0};
    };
}
