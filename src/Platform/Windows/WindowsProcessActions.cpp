#include "Platform/Windows/WindowsProcessActions.h"

#include <windows.h>

#include <algorithm>
#include <map>
#include <string>

namespace tmpp::platform
{
    namespace
    {
        /// Turns a Win32 error into an outcome and a sentence.
        ///
        /// The distinction matters to the user: "the process has already exited" is not a failure to
        /// report, while "access denied" is something they can act on by running elevated.
        [[nodiscard]] ProcessActionResult _fromLastError(DWORD error)
        {
            ProcessActionResult result;

            switch (error)
            {
                case ERROR_INVALID_PARAMETER:
                    // Reading a process that has just exited reports this as readily as a bad id does.
                    result.outcome = ProcessActionOutcome::NotFound;
                    result.message = "The process is no longer running.";
                    break;

                case ERROR_ACCESS_DENIED:
                    result.outcome = ProcessActionOutcome::AccessDenied;
                    result.message = "Access denied. This process needs administrator rights to end.";
                    break;

                case ERROR_NOT_ALL_ASSIGNED:
                case ERROR_PRIVILEGE_NOT_HELD:
                    result.outcome = ProcessActionOutcome::Protected;
                    result.message = "This is a protected system process and cannot be ended.";
                    break;

                default:
                    result.outcome = ProcessActionOutcome::Failed;
                    result.message = "The operation failed with error " + std::to_string(error) + ".";
                    break;
            }

            return result;
        }

        /// Reads a live process's creation time.
        ///
        /// Zero when it cannot be read, which the caller treats as "unable to confirm" rather than as a
        /// mismatch: a process whose creation time cannot be read is not a process to refuse on those
        /// grounds alone.
        [[nodiscard]] uint64_t _creationTimeOf(HANDLE handle)
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

        /// Terminates one process, reporting the outcome rather than logging it.
        ///
        /// @param expectedCreateTime The creation time the caller expects, or zero to skip the check.
        [[nodiscard]] ProcessActionResult _terminateOne(uint32_t pid, uint64_t expectedCreateTime)
        {
            ProcessActionResult result;

            // PROCESS_TERMINATE and PROCESS_QUERY_LIMITED_INFORMATION: the first to end it, the second to
            // confirm it is the process the caller meant. Asking for more would fail on processes where
            // terminating is permitted but inspecting is not.
            constexpr DWORD ACCESS = PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION;

            HANDLE const handle = OpenProcess(ACCESS, FALSE, pid);
            if (handle == nullptr)
            {
                DWORD const error = GetLastError();

                // An access denial does not distinguish a protected process from a pid that a protected
                // process now holds. The second is the ordinary race -- the target exited between the
                // list being drawn and the click -- and reporting it as a permission problem sends the
                // user looking for rights they may already have. When the caller supplied a creation
                // time, a process that cannot be opened at all is a process that is not the one meant,
                // and that is reported as gone.
                if (error == ERROR_ACCESS_DENIED && expectedCreateTime != 0)
                {
                    ProcessActionResult exited;
                    exited.outcome = ProcessActionOutcome::NotFound;
                    exited.message = "The process is no longer running.";
                    return exited;
                }

                return _fromLastError(error);
            }

            // The pid may already have been reused. Acting on the number alone would end an unrelated
            // process, so the creation time is checked first and a mismatch is reported as the process
            // having exited, which is what it is.
            //
            // This is not hypothetical: it was observed in a test, where the second terminate landed on
            // whatever had inherited the pid and reported "access denied" against a system process.
            if (expectedCreateTime != 0)
            {
                uint64_t const live = _creationTimeOf(handle);
                if (live != 0 && live != expectedCreateTime)
                {
                    CloseHandle(handle);

                    ProcessActionResult reused;
                    reused.outcome = ProcessActionOutcome::NotFound;
                    reused.message = "The process has exited; that identifier now belongs to another process.";
                    return reused;
                }
            }

            // Whether the process has already ended, asked before ending it rather than inferred from
            // the error afterwards. GetExitCodeProcess is the canonical way to ask this, and it answers
            // for a process that has terminated while other handles to it remain open -- which is
            // exactly the state a target is in when the user clicks a menu item a moment too late.
            //
            // Terminating such a process fails with an access denial, which is indistinguishable from
            // the denial a protected live process gives. Asking first is what separates the ordinary
            // race from a permission problem, and it is why this is not inferred from the error.
            DWORD exitCode = 0;
            if (GetExitCodeProcess(handle, &exitCode) != FALSE && exitCode != STILL_ACTIVE)
            {
                CloseHandle(handle);

                ProcessActionResult exited;
                exited.outcome = ProcessActionOutcome::NotFound;
                exited.message = "The process is no longer running.";
                return exited;
            }

            BOOL const terminated = TerminateProcess(handle, 1);

            // Read the error before closing: CloseHandle overwrites it.
            DWORD const error = terminated != FALSE ? ERROR_SUCCESS : GetLastError();
            CloseHandle(handle);

            if (terminated == FALSE)
            {
                return _fromLastError(error);
            }

            result.outcome = ProcessActionOutcome::Succeeded;
            result.affected = 1;
            return result;
        }
    }

    ProcessActionResult WindowsProcessActions::Terminate(uint32_t pid, uint64_t expectedCreateTime) const
    {
        // The idle process is not a process that can be ended, and neither is the system process:
        // terminating either is meaningless and the attempt reports a confusing error.
        if (pid == 0 || pid == 4)
        {
            ProcessActionResult result;
            result.outcome = ProcessActionOutcome::Protected;
            result.message = "This is a system process and cannot be ended.";
            return result;
        }

        return _terminateOne(pid, expectedCreateTime);
    }

    ProcessActionResult WindowsProcessActions::TerminateTree(
        uint32_t pid,
        uint64_t expectedCreateTime,
        std::vector<std::pair<uint32_t, uint32_t>> const& parentByPid) const
    {
        if (pid == 0 || pid == 4)
        {
            ProcessActionResult result;
            result.outcome = ProcessActionOutcome::Protected;
            result.message = "This is a system process and cannot be ended.";
            return result;
        }

        // Children of each process, so the tree can be walked from the root down.
        std::map<uint32_t, std::vector<uint32_t>> childrenByParent;
        for (auto const& [child, parent] : parentByPid)
        {
            childrenByParent[parent].push_back(child);
        }

        // Depth-first, collecting the order to terminate in. The root ends up last, and the deepest
        // descendant first, which is the order that stops a parent being killed out from under a child
        // that is still shutting down.
        std::vector<uint32_t> order;
        std::vector<uint32_t> pending{pid};

        // A visited set rather than a depth limit: the snapshot can contain a cycle if a parent id was
        // reused, and a cycle would otherwise be an unbounded walk.
        std::map<uint32_t, bool> visited;

        while (!pending.empty())
        {
            uint32_t const current = pending.back();
            pending.pop_back();

            if (visited[current])
            {
                continue;
            }
            visited[current] = true;

            order.push_back(current);

            if (auto const found = childrenByParent.find(current); found != childrenByParent.end())
            {
                for (uint32_t const child : found->second)
                {
                    pending.push_back(child);
                }
            }
        }

        // Now reversed before applying: the collection order visits a parent before its children, so
        // the reverse visits every child before its parent.
        std::reverse(order.begin(), order.end());

        ProcessActionResult total;
        total.outcome = ProcessActionOutcome::Succeeded;

        for (size_t i = 0; i < order.size(); ++i)
        {
            uint32_t const target = order[i];

            // Only the root carries a creation time from the caller. The descendants come from the
            // snapshot the coordinator just took, which is fresh enough that a reused identifier inside
            // it is not a realistic hazard; the root is the one the user pointed at, and it may have
            // been chosen from a list drawn some time ago.
            bool const isRoot = (target == pid);
            ProcessActionResult const one = _terminateOne(target, isRoot ? expectedCreateTime : 0);
            if (one.Succeeded())
            {
                ++total.affected;
                continue;
            }

            // A child that has already exited on its own is not a failure of the tree operation, which
            // is what happens whenever a process is shutting down while its tree is being ended.
            if (one.outcome == ProcessActionOutcome::NotFound)
            {
                continue;
            }

            ++total.failed;
            if (total.message.empty())
            {
                total.message = one.message;
                total.outcome = one.outcome;
            }
        }

        // Reported as a success when something was stopped and nothing that mattered refused: a tree
        // where one descendant was already gone is a tree that was ended.
        if (total.failed == 0)
        {
            total.outcome = ProcessActionOutcome::Succeeded;
            total.message.clear();
        }

        return total;
    }
}
