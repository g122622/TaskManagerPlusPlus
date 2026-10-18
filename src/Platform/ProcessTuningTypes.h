// The per-process settings the tuning sidebar reads and writes.
//
// Platform-neutral, like the process data types: the Domain and UI layers describe *what* can be
// adjusted without naming a Windows API. The Windows back end maps each enum onto the native
// representation, and the mapping is not always the identity -- the memory priority the kernel takes
// is 1..5 while an I/O priority is 0..3, and the CPU priority is a bit field rather than an ordinal.
// Keeping the project's own numbering here means that arithmetic never leaks into a caller.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace tmpp::platform
{
    /**
     * @brief A process's scheduling priority class.
     *
     * Realtime is present because the operating system has it and a user may ask for it, but it is
     * never offered as an ordinary choice: a realtime process can starve the input and disk threads
     * and make the machine unusable. The UI keeps it out of the picker for that reason.
     */
    enum class ProcessPriorityClass : uint32_t
    {
        Idle = 0,
        BelowNormal,
        Normal,
        AboveNormal,
        High,
        Realtime,
    };

    /**
     * @brief A process's memory priority.
     *
     * Governs how long the memory manager keeps a process's pages resident when it has to trim, and
     * therefore how likely the process is to be paged out under pressure. The numbering matches the
     * kernel's own MEMORY_PRIORITY values so the platform back end is a cast rather than a table.
     */
    enum class ProcessMemoryPriority : uint32_t
    {
        VeryLow = 1,
        Low = 2,
        Medium = 3,
        BelowNormal = 4,
        Normal = 5,
    };

    /**
     * @brief A process's I/O priority.
     *
     * Governs the order the I/O manager issues this process's requests in relative to everyone
     * else's. The numbering matches the kernel's own values. Raising it above Normal needs
     * SeIncreaseBasePriorityPrivilege, which only an elevated process holds.
     */
    enum class ProcessIoPriority : uint32_t
    {
        VeryLow = 0,
        Low = 1,
        Normal = 2,
        High = 3,
    };

    /**
     * @brief Whether a process is in Windows 11's "efficiency mode" (EcoQoS).
     *
     * A three-state setting rather than a switch, because the operating system's own answer is a
     * third state: SystemManaged means Windows decides per-thread, which is what a process that has
     * never been touched reports and is not the same as being explicitly opted out.
     */
    enum class ProcessEfficiencyMode : uint32_t
    {
        /// No explicit request: the operating system applies its own policy.
        SystemManaged = 0,

        /// Explicitly throttled to the efficiency cores and a lower clock.
        On,

        /// Explicitly excluded from throttling, i.e. full speed.
        Off,
    };

    /**
     * @brief Whether the process is suspended.
     *
     * Also three-state: a process is either running or suspended, but a caller may not know which,
     * and "unknown" must not be reported as "running". The kernel exposes no query for suspension --
     * the only way to learn it is to attempt a modification and observe whether it fails -- so this
     * is carried as an option rather than a boolean.
     */
    enum class ProcessSuspendState : uint32_t
    {
        /// Not established.
        Unknown = 0,
        Running,
        Suspended,
    };

    /**
     * @brief Everything the sidebar can read about one process's adjustable settings.
     *
     * Each setting carries its own availability flag rather than sharing one. The APIs behind them
     * arrived in different Windows versions and are refused for different classes of process, so on a
     * real machine some are readable and others are not -- a protected process refuses a memory
     * priority read while still reporting its CPU priority. One flag for the whole struct would have
     * to be pessimistic about all five and would hide the four that did work.
     *
     * A value whose flag is false is meaningless and must not be shown. This is the same rule the
     * rest of the application follows for unavailable metrics: show that it is unknown, never a zero
     * that reads as a measurement.
     */
    struct ProcessTuningState
    {
        bool cpuPriorityAvailable{false};
        bool memoryPriorityAvailable{false};
        bool ioPriorityAvailable{false};
        bool efficiencyModeAvailable{false};
        bool affinityAvailable{false};

        ProcessPriorityClass cpuPriority{ProcessPriorityClass::Normal};
        ProcessMemoryPriority memoryPriority{ProcessMemoryPriority::Normal};
        ProcessIoPriority ioPriority{ProcessIoPriority::Normal};
        ProcessEfficiencyMode efficiencyMode{ProcessEfficiencyMode::SystemManaged};

        /// The set of processors the process may run on, as a bit mask within @c affinityGroup.
        uint64_t affinityMask{0};

        /// The processor group @c affinityMask belongs to.
        ///
        /// Windows splits more than 64 logical processors into groups and the legacy affinity APIs
        /// address one group at a time. Carried explicitly because a mask without its group is
        /// ambiguous on such a machine, and silently assuming group zero would be a wrong answer
        /// rather than a missing one.
        uint16_t affinityGroup{0};

        /// How many logical processors @c affinityGroup holds, so a caller can tell an empty mask
        /// from a mask that simply has bits outside the group.
        uint32_t affinityGroupSize{0};

        /// True when the process's affinity is the whole group, i.e. it has not been restricted.
        ///
        /// A group of exactly 64 is the widest the legacy affinity API can address, and its full mask
        /// is all 64 bits set. That case is spelled out rather than reached through the general
        /// expression below, because `1 << 64` is undefined behaviour: it happens to yield a zero mask
        /// in a debug build and a folded constant in a release one, which is a difference that shows up
        /// as a rule being saved with an affinity restriction the process does not have.
        ///
        /// A group wider than 64 cannot be expressed in one mask at all, and reporting "restricted"
        /// would be a wrong answer where saying nothing is not; callers treat false as "no restriction
        /// worth recording" in either case.
        [[nodiscard]] bool AffinityIsUnrestricted() const noexcept
        {
            if (affinityGroupSize == 0 || affinityGroupSize > 64)
            {
                return false;
            }
            if (affinityGroupSize == 64)
            {
                return affinityMask == ~uint64_t{0};
            }
            return affinityMask == ((uint64_t{1} << affinityGroupSize) - 1);
        }

        ProcessSuspendState suspendState{ProcessSuspendState::Unknown};

        /// True when at least one setting was readable, so the caller can tell "nothing known about
        /// this process" from "this process has no tunable settings".
        [[nodiscard]] bool AnyAvailable() const noexcept
        {
            return cpuPriorityAvailable || memoryPriorityAvailable || ioPriorityAvailable ||
                   efficiencyModeAvailable || affinityAvailable;
        }
    };

    /**
     * @brief A set of settings to change, in one operation.
     *
     * Every member is optional and an unset one is left alone. That is what lets the sidebar stage
     * several edits and apply them with one button without having to send the values the user did not
     * touch: a request that carried the whole state would re-apply settings that were already in
     * effect, which for a priority the process changed itself is a change rather than a no-op.
     */
    struct ProcessTuningChange
    {
        std::optional<ProcessPriorityClass> cpuPriority;
        std::optional<ProcessMemoryPriority> memoryPriority;
        std::optional<ProcessIoPriority> ioPriority;
        std::optional<ProcessEfficiencyMode> efficiencyMode;

        /// A whole-group replacement mask. The group is implied by the process's own.
        std::optional<uint64_t> affinityMask;

        /// True to suspend, false to resume.
        std::optional<bool> suspend;

        [[nodiscard]] bool Empty() const noexcept
        {
            return !cpuPriority.has_value() && !memoryPriority.has_value() && !ioPriority.has_value() &&
                   !efficiencyMode.has_value() && !affinityMask.has_value() && !suspend.has_value();
        }

        /// How many settings the change asks for, so a result can report how many took effect.
        [[nodiscard]] uint32_t Count() const noexcept
        {
            return static_cast<uint32_t>(cpuPriority.has_value()) +
                   static_cast<uint32_t>(memoryPriority.has_value()) +
                   static_cast<uint32_t>(ioPriority.has_value()) +
                   static_cast<uint32_t>(efficiencyMode.has_value()) +
                   static_cast<uint32_t>(affinityMask.has_value()) +
                   static_cast<uint32_t>(suspend.has_value());
        }
    };

    /// Display names for the priority classes, matching the words Windows' own task manager uses.
    [[nodiscard]] inline char const* PriorityClassName(ProcessPriorityClass value) noexcept
    {
        switch (value)
        {
            case ProcessPriorityClass::Idle:
                return "Low";
            case ProcessPriorityClass::BelowNormal:
                return "Below normal";
            case ProcessPriorityClass::AboveNormal:
                return "Above normal";
            case ProcessPriorityClass::High:
                return "High";
            case ProcessPriorityClass::Realtime:
                return "Realtime";
            case ProcessPriorityClass::Normal:
            default:
                return "Normal";
        }
    }

    [[nodiscard]] inline char const* MemoryPriorityName(ProcessMemoryPriority value) noexcept
    {
        switch (value)
        {
            case ProcessMemoryPriority::VeryLow:
                return "Very low";
            case ProcessMemoryPriority::Low:
                return "Low";
            case ProcessMemoryPriority::Medium:
                return "Medium";
            case ProcessMemoryPriority::BelowNormal:
                return "Below normal";
            case ProcessMemoryPriority::Normal:
            default:
                return "Normal";
        }
    }

    [[nodiscard]] inline char const* IoPriorityName(ProcessIoPriority value) noexcept
    {
        switch (value)
        {
            case ProcessIoPriority::VeryLow:
                return "Very low";
            case ProcessIoPriority::Low:
                return "Low";
            case ProcessIoPriority::High:
                return "High";
            case ProcessIoPriority::Normal:
            default:
                return "Normal";
        }
    }

    [[nodiscard]] inline char const* EfficiencyModeName(ProcessEfficiencyMode value) noexcept
    {
        switch (value)
        {
            case ProcessEfficiencyMode::On:
                return "On";
            case ProcessEfficiencyMode::Off:
                return "Off";
            case ProcessEfficiencyMode::SystemManaged:
            default:
                return "Windows decides";
        }
    }
}
