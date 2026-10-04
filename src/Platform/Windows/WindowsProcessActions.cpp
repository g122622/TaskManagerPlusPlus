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

        /// Terminates one process, reporting the outcome rather than logging it.
        [[nodiscard]] ProcessActionResult _terminateOne(uint32_t pid)
        {
            ProcessActionResult result;

            // PROCESS_TERMINATE is the only right this needs. Asking for more would fail on processes
            // where terminating is permitted but inspecting is not.
            HANDLE const handle = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
            if (handle == nullptr)
            {
                return _fromLastError(GetLastError());
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

    ProcessActionResult WindowsProcessActions::Terminate(uint32_t pid) const
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

        return _terminateOne(pid);
    }

    ProcessActionResult WindowsProcessActions::TerminateTree(
        uint32_t pid, std::vector<std::pair<uint32_t, uint32_t>> const& parentByPid) const
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

        for (uint32_t const target : order)
        {
            ProcessActionResult const one = _terminateOne(target);
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
