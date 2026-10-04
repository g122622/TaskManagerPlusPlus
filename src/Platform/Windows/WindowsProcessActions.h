// Process actions on Windows.
//
// Separate from the process probe: the probe reads, this acts, and the two have nothing in common
// besides naming a process. Keeping them apart means a read-only caller cannot accidentally be given
// the ability to terminate something.
//
// Every action here can fail for reasons that are not errors in the ordinary sense -- an access
// denied on a system process is the expected outcome, not a fault -- so the failures are reported
// with enough detail for the UI to say what happened rather than merely that it did not.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "Platform/Result.h"

namespace tmpp::platform
{
    /**
     * @brief Why a process action failed, in terms the user can act on.
     */
    enum class ProcessActionOutcome
    {
        Succeeded = 0,

        /// The process is not running. It may have exited between the list being drawn and the action
        /// being taken, which is normal rather than a fault.
        NotFound,

        /// The operating system refused access. Usually a system process, or one owned by another
        /// user, and the answer is that administrator rights would be needed.
        AccessDenied,

        /// The process is protected. Some system processes refuse termination even to an
        /// administrator, and saying so is clearer than reporting a generic failure.
        Protected,

        /// Anything else. The message carries the operating system's own text.
        Failed,
    };

    /**
     * @brief The result of a process action.
     */
    struct ProcessActionResult
    {
        ProcessActionOutcome outcome{ProcessActionOutcome::Succeeded};

        /// How many processes the action was applied to.
        uint32_t affected{0};

        /// How many could not be, and why the first of them failed.
        uint32_t failed{0};
        std::string message;

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return outcome == ProcessActionOutcome::Succeeded;
        }
    };

    /**
     * @brief Terminates processes on Windows.
     */
    class WindowsProcessActions
    {
    public:
        WindowsProcessActions() = default;

        /**
         * @brief Terminates one process.
         *
         * Uses TerminateProcess, which is not a polite request: the process gets no chance to save
         * anything. That is what the original's "End task" does when the graceful path is unavailable,
         * and it is what the user is asking for when they choose it from a task manager.
         *
         * @param pid Process to terminate.
         */
        [[nodiscard]] ProcessActionResult Terminate(uint32_t pid) const;

        /**
         * @brief Terminates a process and every process descended from it.
         *
         * Children are terminated before their parents, deepest first. Doing it the other way round
         * orphans them: a process whose parent has gone is reparented rather than stopped, so the
         * tree would be left half-killed.
         *
         * @param pid Root of the tree.
         * @param parentByPid The parent of each process in the current snapshot. Used to walk the
         *        tree without enumerating the machine again, which would race the snapshot the user
         *        was looking at.
         */
        [[nodiscard]] ProcessActionResult TerminateTree(
            uint32_t pid, std::vector<std::pair<uint32_t, uint32_t>> const& parentByPid) const;
    };
}
