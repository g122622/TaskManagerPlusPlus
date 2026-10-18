#include "Core/ProcessRules.h"

#include "Core/Logging.h"
#include "Platform/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <string_view>

#include <nlohmann/json.hpp>

namespace tmpp::core
{
    namespace
    {
        using json = nlohmann::json;

        /// The schema version written into the rule file. Rules are stored separately from the
        /// application settings because they are edited on their own schedule and a corrupted rule
        /// set should not take the settings with it.
        constexpr int RULES_VERSION = 1;

        /// The longest a rule file is allowed to be, so a file that is not one cannot be read into
        /// memory without bound.
        constexpr size_t MAX_RULE_FILE_BYTES = 4ull * 1024 * 1024;

        [[nodiscard]] std::string _lowered(std::string_view value)
        {
            std::string lowered{value};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
            return lowered;
        }

        // --------------------------------------------------------------------
        // Enum <-> name
        // --------------------------------------------------------------------
        //
        // Names rather than integers, because the file is meant to be readable and hand-editable and
        // "aboveNormal" says what it means where 3 does not. An unrecognised name leaves the setting
        // unset rather than defaulting it: a rule that recorded a setting the user did not ask for is
        // worse than one that records nothing.

        [[nodiscard]] char const* _cpuPriorityName(platform::ProcessPriorityClass value) noexcept
        {
            switch (value)
            {
                case platform::ProcessPriorityClass::Idle:
                    return "idle";
                case platform::ProcessPriorityClass::BelowNormal:
                    return "belowNormal";
                case platform::ProcessPriorityClass::AboveNormal:
                    return "aboveNormal";
                case platform::ProcessPriorityClass::High:
                    return "high";
                case platform::ProcessPriorityClass::Realtime:
                    return "realtime";
                case platform::ProcessPriorityClass::Normal:
                default:
                    return "normal";
            }
        }

        [[nodiscard]] std::optional<platform::ProcessPriorityClass> _cpuPriorityFrom(std::string_view name)
        {
            if (name == "idle")
            {
                return platform::ProcessPriorityClass::Idle;
            }
            if (name == "belowNormal")
            {
                return platform::ProcessPriorityClass::BelowNormal;
            }
            if (name == "normal")
            {
                return platform::ProcessPriorityClass::Normal;
            }
            if (name == "aboveNormal")
            {
                return platform::ProcessPriorityClass::AboveNormal;
            }
            if (name == "high")
            {
                return platform::ProcessPriorityClass::High;
            }
            if (name == "realtime")
            {
                return platform::ProcessPriorityClass::Realtime;
            }
            return std::nullopt;
        }

        [[nodiscard]] char const* _memoryPriorityName(platform::ProcessMemoryPriority value) noexcept
        {
            switch (value)
            {
                case platform::ProcessMemoryPriority::VeryLow:
                    return "veryLow";
                case platform::ProcessMemoryPriority::Low:
                    return "low";
                case platform::ProcessMemoryPriority::Medium:
                    return "medium";
                case platform::ProcessMemoryPriority::BelowNormal:
                    return "belowNormal";
                case platform::ProcessMemoryPriority::Normal:
                default:
                    return "normal";
            }
        }

        [[nodiscard]] std::optional<platform::ProcessMemoryPriority> _memoryPriorityFrom(std::string_view name)
        {
            if (name == "veryLow")
            {
                return platform::ProcessMemoryPriority::VeryLow;
            }
            if (name == "low")
            {
                return platform::ProcessMemoryPriority::Low;
            }
            if (name == "medium")
            {
                return platform::ProcessMemoryPriority::Medium;
            }
            if (name == "belowNormal")
            {
                return platform::ProcessMemoryPriority::BelowNormal;
            }
            if (name == "normal")
            {
                return platform::ProcessMemoryPriority::Normal;
            }
            return std::nullopt;
        }

        [[nodiscard]] char const* _ioPriorityName(platform::ProcessIoPriority value) noexcept
        {
            switch (value)
            {
                case platform::ProcessIoPriority::VeryLow:
                    return "veryLow";
                case platform::ProcessIoPriority::Low:
                    return "low";
                case platform::ProcessIoPriority::High:
                    return "high";
                case platform::ProcessIoPriority::Normal:
                default:
                    return "normal";
            }
        }

        [[nodiscard]] std::optional<platform::ProcessIoPriority> _ioPriorityFrom(std::string_view name)
        {
            if (name == "veryLow")
            {
                return platform::ProcessIoPriority::VeryLow;
            }
            if (name == "low")
            {
                return platform::ProcessIoPriority::Low;
            }
            if (name == "normal")
            {
                return platform::ProcessIoPriority::Normal;
            }
            if (name == "high")
            {
                return platform::ProcessIoPriority::High;
            }
            return std::nullopt;
        }

        [[nodiscard]] char const* _efficiencyModeName(platform::ProcessEfficiencyMode value) noexcept
        {
            switch (value)
            {
                case platform::ProcessEfficiencyMode::On:
                    return "on";
                case platform::ProcessEfficiencyMode::Off:
                    return "off";
                case platform::ProcessEfficiencyMode::SystemManaged:
                default:
                    return "systemManaged";
            }
        }

        [[nodiscard]] std::optional<platform::ProcessEfficiencyMode> _efficiencyModeFrom(std::string_view name)
        {
            if (name == "systemManaged")
            {
                return platform::ProcessEfficiencyMode::SystemManaged;
            }
            if (name == "on")
            {
                return platform::ProcessEfficiencyMode::On;
            }
            if (name == "off")
            {
                return platform::ProcessEfficiencyMode::Off;
            }
            return std::nullopt;
        }

        /// Reads a string member, or an empty view when it is absent or of another type.
        [[nodiscard]] std::string_view _stringAt(json const& object, char const* key)
        {
            auto const found = object.find(key);
            if (found == object.end() || !found->is_string())
            {
                return {};
            }
            return found->get_ref<std::string const&>();
        }

        [[nodiscard]] json _settingsToJson(platform::ProcessTuningChange const& settings)
        {
            json node = json::object();

            if (settings.cpuPriority.has_value())
            {
                node["cpuPriority"] = _cpuPriorityName(settings.cpuPriority.value());
            }
            if (settings.memoryPriority.has_value())
            {
                node["memoryPriority"] = _memoryPriorityName(settings.memoryPriority.value());
            }
            if (settings.ioPriority.has_value())
            {
                node["ioPriority"] = _ioPriorityName(settings.ioPriority.value());
            }
            if (settings.efficiencyMode.has_value())
            {
                node["efficiencyMode"] = _efficiencyModeName(settings.efficiencyMode.value());
            }
            if (settings.affinityMask.has_value())
            {
                node["affinityMask"] = settings.affinityMask.value();
            }

            return node;
        }

        [[nodiscard]] platform::ProcessTuningChange _settingsFromJson(json const& node)
        {
            platform::ProcessTuningChange settings;
            if (!node.is_object())
            {
                return settings;
            }

            if (auto const value = _cpuPriorityFrom(_stringAt(node, "cpuPriority")); value.has_value())
            {
                settings.cpuPriority = value;
            }
            if (auto const value = _memoryPriorityFrom(_stringAt(node, "memoryPriority")); value.has_value())
            {
                settings.memoryPriority = value;
            }
            if (auto const value = _ioPriorityFrom(_stringAt(node, "ioPriority")); value.has_value())
            {
                settings.ioPriority = value;
            }
            if (auto const value = _efficiencyModeFrom(_stringAt(node, "efficiencyMode")); value.has_value())
            {
                settings.efficiencyMode = value;
            }

            // The affinity mask is written as a number because it is a bit field, not a choice from a
            // list, and a mask that names no processor is refused rather than stored: it would leave a
            // process with nowhere to run.
            if (auto const found = node.find("affinityMask");
                found != node.end() && found->is_number_unsigned() && found->get<uint64_t>() != 0)
            {
                settings.affinityMask = found->get<uint64_t>();
            }

            return settings;
        }
    }

    // ========================================================================
    // ProcessRule
    // ========================================================================

    std::string ProcessRule::Describe() const
    {
        // Built as a list of phrases and joined once, rather than appended with a separator decided at
        // each step. The earlier form had to ask whether the string was still empty before every part,
        // which is where a stray leading separator or a doubled one comes from.
        std::vector<std::string> parts;
        parts.reserve(5);

        if (settings.cpuPriority.has_value())
        {
            parts.push_back(std::string{platform::PriorityClassName(settings.cpuPriority.value())} +
                            " CPU priority");
        }
        if (settings.memoryPriority.has_value())
        {
            parts.push_back(std::string{platform::MemoryPriorityName(settings.memoryPriority.value())} +
                            " memory priority");
        }
        if (settings.ioPriority.has_value())
        {
            parts.push_back(std::string{platform::IoPriorityName(settings.ioPriority.value())} + " I/O priority");
        }
        if (settings.efficiencyMode.has_value())
        {
            parts.push_back(std::string{"Efficiency mode "} +
                            platform::EfficiencyModeName(settings.efficiencyMode.value()));
        }
        if (settings.affinityMask.has_value())
        {
            char buffer[20]{};
            std::snprintf(buffer,
                          sizeof(buffer),
                          "%llX",
                          static_cast<unsigned long long>(settings.affinityMask.value()));
            parts.push_back(std::string{"CPU affinity 0x"} + buffer);
        }

        if (parts.empty())
        {
            return "No settings";
        }

        std::string description = parts.front();
        for (size_t i = 1; i < parts.size(); ++i)
        {
            description += ", ";
            description += parts[i];
        }

        return description;
    }

    // ========================================================================
    // ProcessRuleStore
    // ========================================================================

    std::vector<ProcessRule> ProcessRuleStore::Load() const
    {
        std::vector<ProcessRule> rules;

        if (!platform::FileExists(m_path))
        {
            // No rules yet is the ordinary first-run state, not a problem to report.
            return rules;
        }

        auto const contents = platform::ReadFileToString(m_path);
        if (!contents.Success() || contents.Value().size() > MAX_RULE_FILE_BYTES)
        {
            spdlog::warn("Process rules could not be read from {}; no rules will be applied", m_path);
            return rules;
        }

        // Parsed without exceptions, for the same reason the settings file is: this file is meant to
        // be hand-editable, and a typo in it must cost the user their rules rather than their session.
        json const root = json::parse(contents.Value(), nullptr, false);
        if (root.is_discarded() || !root.is_object())
        {
            spdlog::warn("Process rules file {} is not valid JSON; no rules will be applied", m_path);
            return rules;
        }

        auto const found = root.find("rules");
        if (found == root.end() || !found->is_array())
        {
            return rules;
        }

        rules.reserve(found->size());

        for (auto const& entry : *found)
        {
            if (!entry.is_object())
            {
                continue;
            }

            ProcessRule rule;
            rule.executableName = std::string{_stringAt(entry, "executable")};

            if (auto const enabled = entry.find("enabled"); enabled != entry.end() && enabled->is_boolean())
            {
                rule.enabled = enabled->get<bool>();
            }

            if (auto const settings = entry.find("settings"); settings != entry.end())
            {
                rule.settings = _settingsFromJson(*settings);
            }

            // A rule that names no executable or asks for nothing would never match or never do
            // anything; dropping it here means the matching loop does not have to check.
            if (rule.Valid())
            {
                rules.push_back(std::move(rule));
            }
        }

        return rules;
    }

    VoidResult ProcessRuleStore::Save(std::vector<ProcessRule> const& rules) const
    {
        json root;
        root["version"] = RULES_VERSION;

        json array = json::array();
        for (ProcessRule const& rule : rules)
        {
            if (!rule.Valid())
            {
                continue;
            }

            json entry;
            entry["executable"] = rule.executableName;
            entry["enabled"] = rule.enabled;
            entry["settings"] = _settingsToJson(rule.settings);
            array.push_back(std::move(entry));
        }

        root["rules"] = std::move(array);

        // Indented because the file is meant to be read and edited by hand, exactly like the settings
        // file, and written atomically so an interrupted save cannot leave a truncated rule set behind.
        return platform::AtomicWriteFile(m_path, root.dump(2));
    }

    // ========================================================================
    // ProcessRuleEngine
    // ========================================================================

    uint64_t ProcessRuleEngine::_identityKey(platform::ProcessIdentity const& identity) noexcept
    {
        // The same packing the process model uses to key its own state: a pid alone cannot identify a
        // process, because Windows recycles pids as soon as their processes exit.
        return (static_cast<uint64_t>(identity.pid) << 32) ^ identity.createTime;
    }

    void ProcessRuleEngine::SetRules(std::vector<ProcessRule> rules)
    {
        std::lock_guard const lock(m_mutex);

        m_rules = std::move(rules);

        // Cleared so an edited rule takes effect on the next sample. Leaving it would make the edit
        // appear to do nothing until the process happened to be restarted.
        m_handled.clear();
    }

    std::vector<ProcessRule> ProcessRuleEngine::Rules() const
    {
        std::lock_guard const lock(m_mutex);
        return m_rules;
    }

    bool ProcessRuleEngine::HasActiveRules() const
    {
        std::lock_guard const lock(m_mutex);
        return std::any_of(m_rules.begin(), m_rules.end(), [](ProcessRule const& rule) { return rule.enabled; });
    }

    void ProcessRuleEngine::Evaluate(domain::ProcessSnapshotView const& snapshot, Applier const& apply)
    {
        if (!apply)
        {
            return;
        }

        // The rule set is copied out under the lock and the locks are released before anything is
        // applied. Applying opens a handle and can block, and holding the mutex across that would
        // stall the UI thread the moment it asked to read the rule list.
        std::vector<ProcessRule> rules;
        std::vector<uint64_t> handled;
        {
            std::lock_guard const lock(m_mutex);
            if (m_rules.empty())
            {
                return;
            }
            rules = m_rules;
            handled = m_handled;
        }

        std::sort(handled.begin(), handled.end());

        std::vector<uint64_t> nowHandled;
        nowHandled.reserve(snapshot.processes.size());
        std::vector<ProcessRuleEvent> events;

        for (domain::ProcessView const& process : snapshot.processes)
        {
            uint64_t const key = _identityKey(process.identity);
            nowHandled.push_back(key);

            if (process.imageName.empty())
            {
                // The idle process and the compression store have no image on disk, so no rule can
                // meaningfully name them.
                continue;
            }

            // Already dealt with in an earlier sample. Checked before the name comparison because it
            // is a binary search against a vector of integers, where the comparison would build a
            // lowercase copy of the name for every process on every sample.
            if (std::binary_search(handled.begin(), handled.end(), key))
            {
                continue;
            }

            std::string const loweredName = _lowered(process.imageName);

            for (ProcessRule const& rule : rules)
            {
                if (!rule.enabled || _lowered(rule.executableName) != loweredName)
                {
                    continue;
                }

                ProcessRuleEvent event;
                event.pid = process.identity.pid;
                event.executableName = process.imageName;
                event.result = apply(process.identity.pid, process.identity.createTime, rule.settings);
                events.push_back(std::move(event));

                // One rule per process: two rules naming the same executable would otherwise both be
                // applied in the same round and the second would silently win.
                break;
            }
        }

        std::lock_guard const lock(m_mutex);

        // Rebuilt rather than merged: the handled set only has to describe the processes that exist
        // now, and accumulating would grow it without bound on a machine that starts a process a
        // second.
        m_handled = std::move(nowHandled);

        for (ProcessRuleEvent const& event : events)
        {
            if (event.result.applied > 0)
            {
                ++m_appliedCount;
            }

            // A process that has already exited is not news: a rule fires the moment a process is
            // first observed, and a short-lived one can be gone by the time the change is attempted.
            if (event.result.outcome == platform::ProcessActionOutcome::NotFound)
            {
                continue;
            }

            m_events.push_back(event);
        }
    }

    std::vector<ProcessRuleEvent> ProcessRuleEngine::TakeEvents()
    {
        std::lock_guard const lock(m_mutex);
        std::vector<ProcessRuleEvent> events = std::move(m_events);
        m_events.clear();
        return events;
    }

    uint64_t ProcessRuleEngine::AppliedCount() const
    {
        std::lock_guard const lock(m_mutex);
        return m_appliedCount;
    }
}
