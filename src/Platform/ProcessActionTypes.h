// The outcome of an action taken on a process.
//
// Platform-neutral on purpose: the UI reports what happened to the user, so it needs to name these
// types, and a UI that includes the Windows action header to reach them would be depending on the
// Windows back end. Keeping the vocabulary here means the UI depends on the shape of an answer
// rather than on the machinery that produced it.
//
// The distinction between the outcomes matters to the user rather than to the caller's control flow:
// "the process has already exited" is not a failure to act on, while "access denied" is something
// they can fix by running elevated. Collapsing them into one boolean is what turns a permission
// problem into a message that says nothing.
#pragma once

#include <cstdint>
#include <string>

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

        /// The process is protected. Some system processes refuse modification even to an
        /// administrator, and saying so is clearer than reporting a generic failure.
        Protected,

        /// The request named a setting this operating system or this process does not have. Reported
        /// apart from a generic failure because there is nothing the user can do about it.
        NotSupported,

        /// Anything else. The message carries the operating system's own text.
        Failed,
    };

    /**
     * @brief The result of a process action.
     *
     * For an operation that changes several things at once -- which is what the tuning sidebar asks
     * for, since it stages a set of edits and applies them together -- @c applied names what did take
     * effect and @c field names the first thing that did not. A partial result is otherwise
     * impossible to describe: the process is in neither its old state nor the requested one.
     */
    struct ProcessActionResult
    {
        ProcessActionOutcome outcome{ProcessActionOutcome::Succeeded};

        /// How many processes the action was applied to.
        uint32_t affected{0};

        /// How many could not be, and why the first of them failed.
        uint32_t failed{0};
        std::string message;

        /**
         * @brief Which requested setting failed, empty when none did.
         *
         * A short identifier rather than a sentence fragment, so a caller can label the row the user
         * was editing without parsing a message.
         */
        std::string field;

        /// How many settings were requested and how many of those took effect.
        uint32_t requested{0};
        uint32_t applied{0};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return outcome == ProcessActionOutcome::Succeeded;
        }

        /// True when some but not all of the requested settings took effect.
        [[nodiscard]] bool PartiallyApplied() const noexcept
        {
            return applied > 0 && applied < requested;
        }
    };
}
