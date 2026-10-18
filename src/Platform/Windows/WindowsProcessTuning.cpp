#include "Platform/Windows/WindowsProcessTuning.h"

#include "Platform/Windows/WindowsString.h"

#include <windows.h>

#include <winternl.h>

#include <cstdio>
#include <string>

namespace tmpp::platform
{
    namespace
    {
        // ====================================================================
        // Native entry points
        // ====================================================================
        //
        // The ntdll functions are resolved at run time for the same reason the probes resolve
        // NtQuerySystemInformation that way: they are the documented-stable but unpublished native
        // API, and linking them statically would tie the binary to an import library that the WDK
        // supplies rather than the SDK.

        using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG, PULONG);
        using NtSetInformationProcessFn = LONG(NTAPI*)(HANDLE, ULONG, PVOID, ULONG);
        using NtSuspendResumeProcessFn = LONG(NTAPI*)(HANDLE);

        template <typename T>
        [[nodiscard]] T _ntdllEntry(char const* name) noexcept
        {
            static HMODULE const ntdll = GetModuleHandleW(L"ntdll.dll");
            if (ntdll == nullptr)
            {
                return nullptr;
            }
            return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(ntdll, name)));
        }

        [[nodiscard]] NtQueryInformationProcessFn _ntQueryInformationProcess() noexcept
        {
            static NtQueryInformationProcessFn const fn = _ntdllEntry<NtQueryInformationProcessFn>("NtQueryInformationProcess");
            return fn;
        }

        [[nodiscard]] NtSetInformationProcessFn _ntSetInformationProcess() noexcept
        {
            static NtSetInformationProcessFn const fn = _ntdllEntry<NtSetInformationProcessFn>("NtSetInformationProcess");
            return fn;
        }

        [[nodiscard]] NtSuspendResumeProcessFn _ntSuspendProcess() noexcept
        {
            static NtSuspendResumeProcessFn const fn = _ntdllEntry<NtSuspendResumeProcessFn>("NtSuspendProcess");
            return fn;
        }

        [[nodiscard]] NtSuspendResumeProcessFn _ntResumeProcess() noexcept
        {
            static NtSuspendResumeProcessFn const fn = _ntdllEntry<NtSuspendResumeProcessFn>("NtResumeProcess");
            return fn;
        }

        // ====================================================================
        // Constants
        // ====================================================================

        /// The native information class for a process's I/O priority.
        ///
        /// Not part of the published PROCESS_INFORMATION_CLASS enum, which stops at the kernel32
        /// surface; the native class is numbered from a separate sequence and I/O priority is 33 in
        /// it. The value has been stable since Windows Vista and is the only way to reach this
        /// setting: there is no kernel32 wrapper for it.
        constexpr ULONG PROCESS_IO_PRIORITY_CLASS = 33;

        /// The privilege that raising a priority above Normal requires.
        ///
        /// Held by an elevated process and absent otherwise. Requesting it and finding it is not
        /// there is the expected outcome for a normal user, not a fault, which is why the result is
        /// inspected rather than the call's return value alone.
        constexpr wchar_t const* SE_INCREASE_BASE_PRIORITY = L"SeIncreaseBasePriorityPrivilege";

        /// NTSTATUS codes the native calls report, in the terms the caller acts on.
        ///
        /// Named with the NT prefix rather than the STATUS_ one the SDK uses, because winnt.h
        /// already defines several of these as macros. A matching name here would be replaced by the
        /// macro and turn the declaration into nonsense.
        constexpr LONG NT_STATUS_ACCESS_DENIED = static_cast<LONG>(0xC0000022);
        constexpr LONG NT_STATUS_INVALID_HANDLE = static_cast<LONG>(0xC0000008);
        constexpr LONG NT_STATUS_PRIVILEGE_NOT_HELD = static_cast<LONG>(0xC0000061);
        constexpr LONG NT_STATUS_PROCESS_IS_TERMINATING = static_cast<LONG>(0xC000010A);

        // ====================================================================
        // Small helpers
        // ====================================================================

        [[nodiscard]] ProcessPriorityClass _toPriorityClass(DWORD value) noexcept
        {
            switch (value)
            {
                case IDLE_PRIORITY_CLASS:
                    return ProcessPriorityClass::Idle;
                case BELOW_NORMAL_PRIORITY_CLASS:
                    return ProcessPriorityClass::BelowNormal;
                case ABOVE_NORMAL_PRIORITY_CLASS:
                    return ProcessPriorityClass::AboveNormal;
                case HIGH_PRIORITY_CLASS:
                    return ProcessPriorityClass::High;
                case REALTIME_PRIORITY_CLASS:
                    return ProcessPriorityClass::Realtime;
                case NORMAL_PRIORITY_CLASS:
                default:
                    return ProcessPriorityClass::Normal;
            }
        }

        [[nodiscard]] DWORD _toWin32Priority(ProcessPriorityClass value) noexcept
        {
            switch (value)
            {
                case ProcessPriorityClass::Idle:
                    return IDLE_PRIORITY_CLASS;
                case ProcessPriorityClass::BelowNormal:
                    return BELOW_NORMAL_PRIORITY_CLASS;
                case ProcessPriorityClass::AboveNormal:
                    return ABOVE_NORMAL_PRIORITY_CLASS;
                case ProcessPriorityClass::High:
                    return HIGH_PRIORITY_CLASS;
                case ProcessPriorityClass::Realtime:
                    return REALTIME_PRIORITY_CLASS;
                case ProcessPriorityClass::Normal:
                default:
                    return NORMAL_PRIORITY_CLASS;
            }
        }

        /// Maps a Win32 error to the outcome the user is shown.
        ///
        /// The same mapping WindowsProcessActions uses, kept separate rather than shared because the
        /// kindest wording differs: an access denial while ending a process is worth suggesting
        /// elevation for, while an access denial while changing a priority usually means the process
        /// is protected by something elevation does not get past.
        [[nodiscard]] ProcessActionOutcome _outcomeForError(DWORD error) noexcept
        {
            switch (error)
            {
                case ERROR_INVALID_PARAMETER:
                case ERROR_NOT_FOUND:
                    return ProcessActionOutcome::NotFound;
                case ERROR_ACCESS_DENIED:
                case ERROR_PRIVILEGE_NOT_HELD:
                case ERROR_NOT_ALL_ASSIGNED:
                    return ProcessActionOutcome::AccessDenied;
                case ERROR_CALL_NOT_IMPLEMENTED:
                case ERROR_INVALID_FUNCTION:
                case ERROR_NOT_SUPPORTED:
                    return ProcessActionOutcome::NotSupported;
                default:
                    return ProcessActionOutcome::Failed;
            }
        }

        [[nodiscard]] ProcessActionOutcome _outcomeForStatus(LONG status) noexcept
        {
            switch (status)
            {
                case NT_STATUS_ACCESS_DENIED:
                case NT_STATUS_PRIVILEGE_NOT_HELD:
                    return ProcessActionOutcome::AccessDenied;
                case NT_STATUS_INVALID_HANDLE:
                case NT_STATUS_PROCESS_IS_TERMINATING:
                    return ProcessActionOutcome::NotFound;
                default:
                    return ProcessActionOutcome::Failed;
            }
        }

        [[nodiscard]] std::string _describe(DWORD error)
        {
            switch (error)
            {
                case ERROR_ACCESS_DENIED:
                    return "access denied; the process is protected or needs administrator rights.";
                case ERROR_PRIVILEGE_NOT_HELD:
                case ERROR_NOT_ALL_ASSIGNED:
                    return "the required privilege is not held; run as administrator.";
                case ERROR_INVALID_PARAMETER:
                    return "the process is no longer running.";
                case ERROR_NOT_SUPPORTED:
                case ERROR_CALL_NOT_IMPLEMENTED:
                    return "this Windows version or this process does not support the setting.";
                default:
                    return "the operating system reported error " + std::to_string(error) + ".";
            }
        }

        [[nodiscard]] std::string _describeStatus(LONG status)
        {
            char buffer[16]{};
            std::snprintf(buffer, sizeof(buffer), "0x%08lX", static_cast<unsigned long>(status));
            return "the native call returned " + std::string{buffer} + ".";
        }

        /// Reads a live process's creation time, or zero when it cannot be read.
        [[nodiscard]] uint64_t _creationTimeOf(HANDLE handle) noexcept
        {
            FILETIME creation{};
            FILETIME exit{};
            FILETIME kernel{};
            FILETIME user{};
            if (GetProcessTimes(handle, &creation, &exit, &kernel, &user) == FALSE)
            {
                return 0;
            }

            ULARGE_INTEGER value{};
            value.LowPart = creation.dwLowDateTime;
            value.HighPart = creation.dwHighDateTime;
            return value.QuadPart;
        }

        /// Whether a process that has been opened is still running.
        ///
        /// Asked rather than inferred from a later failure: a process that has exited reports an
        /// access denial for most operations, which is indistinguishable from the denial a live
        /// protected process gives. Separating the two is what stops the user being sent to look for
        /// rights they already have.
        [[nodiscard]] bool _isStillRunning(HANDLE handle) noexcept
        {
            DWORD exitCode = 0;
            return GetExitCodeProcess(handle, &exitCode) == FALSE || exitCode == STILL_ACTIVE;
        }

        /**
         * @brief Enables one privilege on the current process for the length of a scope.
         *
         * Needed for the two settings Windows refuses to raise above Normal without it. The privilege
         * is held by an elevated token, so enabling it on a normal one fails, which is reported as
         * "not held" rather than as an error: that is the ordinary outcome for a user who has not
         * elevated, and the operation that needed it is what gets reported as refused.
         */
        class ScopedPrivilege
        {
        public:
            explicit ScopedPrivilege(wchar_t const* name) noexcept
            {
                if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &m_token) == FALSE)
                {
                    m_token = nullptr;
                    return;
                }

                LUID luid{};
                if (LookupPrivilegeValueW(nullptr, name, &luid) == FALSE)
                {
                    return;
                }

                TOKEN_PRIVILEGES requested{};
                requested.PrivilegeCount = 1;
                requested.Privileges[0].Luid = luid;
                requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

                DWORD returned = 0;
                if (AdjustTokenPrivileges(m_token, FALSE, &requested, sizeof(m_previous), &m_previous, &returned) == FALSE)
                {
                    return;
                }

                // AdjustTokenPrivileges reports success even when it assigned nothing, so the error
                // is the only way to learn that the privilege was not there to enable. Enabling one
                // the token does not hold is not a failure of this scope: the caller's operation will
                // fail on its own and report a better message.
                if (GetLastError() == ERROR_NOT_ALL_ASSIGNED)
                {
                    return;
                }

                m_enabled = true;
            }

            ~ScopedPrivilege()
            {
                if (m_enabled)
                {
                    // The previous state is put back so the elevation this application holds is not
                    // quietly widened for the rest of its life by a control the user touched once.
                    AdjustTokenPrivileges(m_token, FALSE, &m_previous, 0, nullptr, nullptr);
                }

                if (m_token != nullptr)
                {
                    CloseHandle(m_token);
                }
            }

            ScopedPrivilege(ScopedPrivilege const&) = delete;
            ScopedPrivilege& operator=(ScopedPrivilege const&) = delete;

            [[nodiscard]] bool Enabled() const noexcept { return m_enabled; }

        private:
            HANDLE m_token{nullptr};
            TOKEN_PRIVILEGES m_previous{};
            bool m_enabled{false};
        };

        /// How a requested setting is named in a failure. Matched to the labels the sidebar shows so
        /// the message and the row it refers to use the same word.
        [[nodiscard]] char const* _fieldLabel(std::string const& field) noexcept
        {
            if (field == "cpuPriority")
            {
                return "CPU priority";
            }
            if (field == "memoryPriority")
            {
                return "Memory priority";
            }
            if (field == "ioPriority")
            {
                return "I/O priority";
            }
            if (field == "efficiencyMode")
            {
                return "Efficiency mode";
            }
            if (field == "affinityMask")
            {
                return "CPU affinity";
            }
            if (field == "suspend")
            {
                return "Suspend";
            }
            return "Setting";
        }

        /// Records one requested setting's outcome into the aggregate result.
        void _record(ProcessActionResult& result,
                     char const* field,
                     bool succeeded,
                     ProcessActionResult const& failure)
        {
            if (succeeded)
            {
                ++result.applied;
                return;
            }

            ++result.failed;

            // Only the first failure is described. A caller that tried four settings and had three
            // refused has one problem to solve, and reporting the last one would hide the first.
            if (result.message.empty())
            {
                result.field = field;
                result.outcome = failure.outcome;
                result.message = failure.message;
            }
        }
    }

    // ========================================================================
    // Reading
    // ========================================================================

    Result<ProcessTuningState> WindowsProcessTuning::Read(uint32_t pid) const
    {
        if (pid == 0)
        {
            return Error{ErrorCode::InvalidArgument,
                         "The idle process has no tunable settings.",
                         "WindowsProcessTuning::Read"};
        }

        // QUERY_LIMITED_INFORMATION rather than QUERY_INFORMATION: the limited right is granted to
        // more processes, and none of the readings below needs more than it.
        HANDLE const handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (handle == nullptr)
        {
            DWORD const error = GetLastError();
            return Error{error == ERROR_ACCESS_DENIED ? ErrorCode::AccessDenied : ErrorCode::NotFound,
                         _describe(error),
                         "WindowsProcessTuning::Read"};
        }

        ProcessTuningState state;

        // --- CPU priority ---
        DWORD const priorityClass = GetPriorityClass(handle);
        if (priorityClass != 0)
        {
            state.cpuPriority = _toPriorityClass(priorityClass);
            state.cpuPriorityAvailable = true;
        }

        // --- Memory priority ---
        {
            MEMORY_PRIORITY_INFORMATION information{};
            // The information class is qualified with the global scope operator: this project has an
            // enum of its own called ProcessMemoryPriority, which would otherwise shadow the SDK's
            // enumerator of the same name and make this a type where a value was wanted.
            if (GetProcessInformation(handle,
                                      ::ProcessMemoryPriority,
                                      &information,
                                      sizeof(information)) != FALSE)
            {
                // The kernel's values are numbered 0..5 while this project's enum starts at 1, so the
                // native value is matched rather than cast. An unrecognised value leaves the setting
                // unavailable instead of being forced into the nearest name.
                if (information.MemoryPriority >= static_cast<ULONG>(ProcessMemoryPriority::VeryLow) &&
                    information.MemoryPriority <= static_cast<ULONG>(ProcessMemoryPriority::Normal))
                {
                    state.memoryPriority = static_cast<ProcessMemoryPriority>(information.MemoryPriority);
                    state.memoryPriorityAvailable = true;
                }
            }
        }

        // --- I/O priority ---
        if (auto const query = _ntQueryInformationProcess(); query != nullptr)
        {
            ULONG value = 0;
            ULONG returned = 0;
            if (query(handle, PROCESS_IO_PRIORITY_CLASS, &value, sizeof(value), &returned) == 0 &&
                value <= static_cast<ULONG>(ProcessIoPriority::High))
            {
                state.ioPriority = static_cast<ProcessIoPriority>(value);
                state.ioPriorityAvailable = true;
            }
        }

        // --- Efficiency mode ---
        {
            PROCESS_POWER_THROTTLING_STATE throttling{};
            throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;

            if (GetProcessInformation(handle,
                                      ProcessPowerThrottling,
                                      &throttling,
                                      sizeof(throttling)) != FALSE)
            {
                // Three states out of two bits: the control mask says whether the caller has an
                // opinion at all, and the state mask says what that opinion is. A process that has
                // never been touched reports no opinion, which is not the same as being explicitly
                // excluded from throttling -- collapsing those two into "off" is what makes Windows'
                // own task manager show an incorrect value after a process has been toggled.
                if ((throttling.ControlMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) == 0)
                {
                    state.efficiencyMode = ProcessEfficiencyMode::SystemManaged;
                }
                else if ((throttling.StateMask & PROCESS_POWER_THROTTLING_EXECUTION_SPEED) != 0)
                {
                    state.efficiencyMode = ProcessEfficiencyMode::On;
                }
                else
                {
                    state.efficiencyMode = ProcessEfficiencyMode::Off;
                }

                state.efficiencyModeAvailable = true;
            }
        }

        // --- Affinity ---
        {
            DWORD_PTR processMask = 0;
            DWORD_PTR systemMask = 0;
            if (GetProcessAffinityMask(handle, &processMask, &systemMask) != FALSE)
            {
                state.affinityAvailable = true;
                state.affinityMask = static_cast<uint64_t>(processMask);

                // The legacy affinity API addresses one processor group, and the mask it returns is
                // the one for the group the process's threads live in. On a machine with a single
                // group -- which is every machine with 64 or fewer logical processors -- that is group
                // zero and this is exact. On a larger machine the group is reported as zero and the
                // size is the whole active set, which the UI states rather than pretending to a
                // precision the API does not offer.
                state.affinityGroup = 0;
                state.affinityGroupSize = ActiveProcessorCount();
            }
        }

        CloseHandle(handle);

        // Conservative on purpose: a process that is being torn down can have no threads for an
        // instant, and reporting that as suspended would show the user a state they did not choose.
        state.suspendState = ProcessSuspendState::Unknown;

        return state;
    }

    // ========================================================================
    // Writing
    // ========================================================================

    ProcessActionResult WindowsProcessTuning::Apply(uint32_t pid,
                                                    uint64_t expectedCreateTime,
                                                    ProcessTuningChange const& change) const
    {
        ProcessActionResult result;
        result.requested = change.Count();

        if (pid == 0 || pid == 4)
        {
            // The idle process and the system process have no user-facing scheduling state, and
            // asking for one reports an error that says nothing useful about why.
            result.outcome = ProcessActionOutcome::Protected;
            result.failed = result.requested;
            result.message = "This is a system process and its settings cannot be changed.";
            return result;
        }

        if (change.Empty())
        {
            result.outcome = ProcessActionOutcome::Succeeded;
            return result;
        }

        DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION;
        if (change.suspend.has_value())
        {
            access |= PROCESS_SUSPEND_RESUME;
        }

        HANDLE const handle = OpenProcess(access, FALSE, pid);
        if (handle == nullptr)
        {
            DWORD const error = GetLastError();

            // An access denial against a process whose creation time was supplied is the ordinary
            // race -- the target exited and its identifier now belongs to something else -- and
            // reporting it as a permission problem would send the user to look for rights they may
            // already have.
            if (error == ERROR_ACCESS_DENIED && expectedCreateTime != 0)
            {
                result.outcome = ProcessActionOutcome::NotFound;
                result.failed = result.requested;
                result.message = "The process is no longer running.";
                return result;
            }

            result.outcome = _outcomeForError(error);
            result.failed = result.requested;
            result.message = "The process could not be opened: " + _describe(error);
            return result;
        }

        // The identifier may already have been reused, and acting on the number alone would change a
        // process the user never selected. The check is skipped when the caller has no creation time
        // to compare against, which is the case for a caller acting on a process it just opened.
        if (expectedCreateTime != 0)
        {
            uint64_t const live = _creationTimeOf(handle);
            if (live != 0 && live != expectedCreateTime)
            {
                CloseHandle(handle);
                result.outcome = ProcessActionOutcome::NotFound;
                result.failed = result.requested;
                result.message = "The process has exited; that identifier now belongs to another process.";
                return result;
            }
        }

        if (!_isStillRunning(handle))
        {
            CloseHandle(handle);
            result.outcome = ProcessActionOutcome::NotFound;
            result.failed = result.requested;
            result.message = "The process is no longer running.";
            return result;
        }

        // ------------------------------------------------------------------
        // CPU priority
        // ------------------------------------------------------------------
        if (change.cpuPriority.has_value())
        {
            ProcessActionResult failure;
            bool succeeded = false;

            // A priority above Normal needs the privilege; asking for it first means the failure is
            // reported against the setting rather than as an opaque access denial.
            ScopedPrivilege privilege(SE_INCREASE_BASE_PRIORITY);

            if (SetPriorityClass(handle, _toWin32Priority(change.cpuPriority.value())) != FALSE)
            {
                succeeded = true;
            }
            else
            {
                DWORD const error = GetLastError();
                failure.outcome = error == ERROR_ACCESS_DENIED && !privilege.Enabled()
                                      ? ProcessActionOutcome::AccessDenied
                                      : _outcomeForError(error);
                failure.message = "CPU priority could not be changed: " + _describe(error);
            }

            _record(result, "cpuPriority", succeeded, failure);
        }

        // ------------------------------------------------------------------
        // Memory priority
        // ------------------------------------------------------------------
        if (change.memoryPriority.has_value())
        {
            ProcessActionResult failure;
            bool succeeded = false;

            MEMORY_PRIORITY_INFORMATION information{};
            information.MemoryPriority = static_cast<ULONG>(change.memoryPriority.value());

            if (SetProcessInformation(handle,
                                      ::ProcessMemoryPriority,
                                      &information,
                                      sizeof(information)) != FALSE)
            {
                succeeded = true;
            }
            else
            {
                DWORD const error = GetLastError();
                failure.outcome = _outcomeForError(error);
                failure.message = "Memory priority could not be changed: " + _describe(error);
            }

            _record(result, "memoryPriority", succeeded, failure);
        }

        // ------------------------------------------------------------------
        // I/O priority
        // ------------------------------------------------------------------
        if (change.ioPriority.has_value())
        {
            ProcessActionResult failure;
            bool succeeded = false;
            // Not const: the native call takes a mutable pointer, because the information classes
            // share one prototype and some of them return data through it.
            ULONG value = static_cast<ULONG>(change.ioPriority.value());

            auto const set = _ntSetInformationProcess();
            if (set == nullptr)
            {
                failure.outcome = ProcessActionOutcome::NotSupported;
                failure.message = "I/O priority is not available on this Windows version.";
            }
            else
            {
                // Raising it above Normal is the same privilege the CPU priority needs.
                ScopedPrivilege privilege(SE_INCREASE_BASE_PRIORITY);
                LONG const status = set(handle, PROCESS_IO_PRIORITY_CLASS, &value, sizeof(value));

                if (status == 0)
                {
                    succeeded = true;
                }
                else
                {
                    failure.outcome = _outcomeForStatus(status);
                    failure.message = "I/O priority could not be changed: " + _describeStatus(status);
                }
            }

            _record(result, "ioPriority", succeeded, failure);
        }

        // ------------------------------------------------------------------
        // Efficiency mode
        // ------------------------------------------------------------------
        if (change.efficiencyMode.has_value())
        {
            ProcessActionResult failure;
            bool succeeded = false;

            PROCESS_POWER_THROTTLING_STATE throttling{};
            throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;

            switch (change.efficiencyMode.value())
            {
                case ProcessEfficiencyMode::SystemManaged:
                    // Withdrawing the request rather than requesting the opposite: a process the user
                    // has not expressed an opinion about is governed by the operating system's own
                    // policy, which is a different state from being explicitly opted out.
                    throttling.ControlMask = 0;
                    throttling.StateMask = 0;
                    break;

                case ProcessEfficiencyMode::On:
                    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                    throttling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                    break;

                case ProcessEfficiencyMode::Off:
                    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
                    throttling.StateMask = 0;
                    break;
            }

            if (SetProcessInformation(handle,
                                      ProcessPowerThrottling,
                                      &throttling,
                                      sizeof(throttling)) != FALSE)
            {
                succeeded = true;
            }
            else
            {
                DWORD const error = GetLastError();
                failure.outcome = _outcomeForError(error);
                failure.message = "Efficiency mode could not be changed: " + _describe(error);
            }

            _record(result, "efficiencyMode", succeeded, failure);
        }

        // ------------------------------------------------------------------
        // Affinity
        // ------------------------------------------------------------------
        if (change.affinityMask.has_value())
        {
            ProcessActionResult failure;
            bool succeeded = false;

            uint64_t const requested = change.affinityMask.value();

            if (requested == 0)
            {
                // An empty mask would leave the process with nowhere to run. Windows refuses it, and
                // refusing it here says why instead of relaying an invalid-parameter error.
                failure.outcome = ProcessActionOutcome::Failed;
                failure.message = "CPU affinity cannot be empty; leave at least one processor selected.";
            }
            else if (SetProcessAffinityMask(handle, static_cast<DWORD_PTR>(requested)) != FALSE)
            {
                succeeded = true;
            }
            else
            {
                DWORD const error = GetLastError();
                failure.outcome = _outcomeForError(error);

                // ERROR_INVALID_PARAMETER here means the mask named a processor that does not exist
                // or does not belong to the process's group, which is worth saying precisely: the
                // generic mapping would report it as the process having exited.
                failure.message = error == ERROR_INVALID_PARAMETER
                                      ? std::string{"CPU affinity was rejected: the mask names a processor "
                                                    "this process cannot use."}
                                      : "CPU affinity could not be changed: " + _describe(error);

                if (error == ERROR_INVALID_PARAMETER)
                {
                    failure.outcome = ProcessActionOutcome::Failed;
                }
            }

            _record(result, "affinityMask", succeeded, failure);
        }

        // ------------------------------------------------------------------
        // Suspension, last
        // ------------------------------------------------------------------
        if (change.suspend.has_value())
        {
            ProcessActionResult failure;
            bool succeeded = false;

            bool const wantSuspend = change.suspend.value();

            auto const fn = wantSuspend ? _ntSuspendProcess() : _ntResumeProcess();
            if (fn == nullptr)
            {
                failure.outcome = ProcessActionOutcome::NotSupported;
                failure.message = "Suspend and resume are not available on this Windows version.";
            }
            else
            {
                LONG const status = fn(handle);
                if (status == 0)
                {
                    succeeded = true;
                }
                else
                {
                    failure.outcome = _outcomeForStatus(status);
                    failure.message = std::string{wantSuspend ? "The process could not be suspended: "
                                                              : "The process could not be resumed: "} +
                                      _describeStatus(status);
                }
            }

            _record(result, "suspend", succeeded, failure);
        }

        CloseHandle(handle);

        // Every requested setting took effect. The message is left empty by the individual steps, so
        // this is the one place a success is stated.
        if (result.applied == result.requested)
        {
            result.outcome = ProcessActionOutcome::Succeeded;
            result.message.clear();
            result.field.clear();
        }
        else if (result.applied > 0)
        {
            // A partial application is neither a success nor a plain failure, and the caller needs to
            // know which it was: the process is now in a state the user did not ask for in full, and
            // the panel says what did and did not take effect.
            result.message = std::string{_fieldLabel(result.field)} + " was not applied (" +
                             result.message + ") " + std::to_string(result.applied) + " of " +
                             std::to_string(result.requested) + " changes took effect.";
        }
        else
        {
            result.message = std::string{_fieldLabel(result.field)} + ": " + result.message;
        }

        return result;
    }

    // ========================================================================
    // Path
    // ========================================================================

    std::string WindowsProcessTuning::ExecutablePath(uint32_t pid) const
    {
        if (pid == 0)
        {
            return {};
        }

        HANDLE const handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (handle == nullptr)
        {
            return {};
        }

        // Grows from a size that covers almost every path and doubles until it fits or the ceiling is
        // reached. A fixed buffer truncates a long path, and a truncated path is worse than no path
        // at all: it names a file that does not exist, and the user is offered an "open file location"
        // for a directory that is not where the process came from.
        //
        // The format is UTF-16, so the count is in characters and the byte size is twice it.
        std::wstring buffer(512, L'\0');
        constexpr DWORD MAX_PATH_CHARACTERS = 64 * 1024;

        for (;;)
        {
            DWORD length = static_cast<DWORD>(buffer.size());
            if (QueryFullProcessImageNameW(handle, 0, buffer.data(), &length) != FALSE)
            {
                CloseHandle(handle);
                return ToUtf8(buffer.data(), length);
            }

            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= MAX_PATH_CHARACTERS)
            {
                CloseHandle(handle);
                return {};
            }

            buffer.resize(buffer.size() * 2);
        }
    }

    uint32_t WindowsProcessTuning::ActiveProcessorCount() noexcept
    {
        DWORD const count = GetActiveProcessorCount(0);
        return count > 0 ? count : static_cast<uint32_t>(GetMaximumProcessorCount(0));
    }

    uint32_t WindowsProcessTuning::ActiveProcessorGroupCount() noexcept
    {
        return static_cast<uint32_t>(GetActiveProcessorGroupCount());
    }
}
