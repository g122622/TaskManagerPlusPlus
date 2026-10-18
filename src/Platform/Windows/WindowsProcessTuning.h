// Reading and changing a single process's adjustable settings on Windows.
//
// Separate from the process probe (which reads counters for every process in one call) and from
// WindowsProcessActions (which ends processes). The three have nothing in common besides naming a
// process: this one opens exactly one process at a time, on demand, because the values it reads are
// not counters that can be differenced or batched -- they are settings, and each one needs a handle
// with a different access mask to reach.
//
// That is also why nothing here runs on the sampling thread. A bulk counter read has to be cheap
// because it happens every second; a settings read happens when the user selects a row.
//
// Two rules the callers depend on:
//
//   1. A refused API is not an error. Some settings are unavailable on some processes and some
//      Windows builds do not have all of them, so each is reported as unavailable rather than
//      failing the whole read.
//   2. Nothing is inferred. Where the kernel offers no query -- the suspend state is the only
//      example -- the answer is "unknown" rather than a guess, and the caller is told so.
#pragma once

#include <cstdint>
#include <string>

#include "Platform/ProcessActionTypes.h"
#include "Platform/ProcessTuningTypes.h"
#include "Platform/Result.h"

namespace tmpp::platform
{
    /**
     * @brief Reads and writes one process's scheduling, memory, I/O, power and affinity settings.
     *
     * Stateless: every call opens its own handle and closes it again. Holding a handle per process
     * would mean tracking process exits and would keep a process alive after the user had finished
     * with it, which is a worse trade than the open, which is a few microseconds on a path that runs
     * once per selection.
     */
    class WindowsProcessTuning
    {
    public:
        WindowsProcessTuning() = default;

        /**
         * @brief Reads every setting that can be read for one process.
         *
         * Fails only when the process cannot be opened at all, which means there is nothing to say
         * about it. A process that opens but refuses an individual setting reports that setting as
         * unavailable and the rest as read: a protected process refuses its memory priority while
         * still reporting its CPU priority, and collapsing those into one failure would hide four
         * answers because of the fifth.
         *
         * The suspend state is deliberately left Unknown. The kernel has no query for it; the
         * coordinator fills it in from the process snapshot, which is the only place the question can
         * be answered without a modification attempt.
         */
        [[nodiscard]] Result<ProcessTuningState> Read(uint32_t pid) const;

        /**
         * @brief Applies the requested settings to one process.
         *
         * The settings are applied in a fixed order chosen so that a partial failure leaves the
         * process in the most recoverable state: scheduling and memory first, because they are the
         * ones a user notices, then I/O, then power, then affinity, and suspension last because a
         * suspended process is the one state a caller cannot easily undo.
         *
         * @param pid Process to change.
         * @param expectedCreateTime The creation time the caller believes this process has, or zero
         *        to skip the check. A pid is reused as soon as its process exits, so acting on the
         *        number alone can change a process the user never selected. A mismatch is reported as
         *        the process having exited rather than as a fault.
         * @param change What to change. Unset members are left alone.
         */
        [[nodiscard]] ProcessActionResult Apply(uint32_t pid,
                                                uint64_t expectedCreateTime,
                                                ProcessTuningChange const& change) const;

        /**
         * @brief The process's executable path, in UTF-8.
         *
         * Empty when it cannot be read, which is an ordinary outcome for a protected process rather
         * than a fault. Read with a growing buffer because a fixed one truncates a long path, and a
         * truncated path is worse than none: it names a file that does not exist.
         */
        [[nodiscard]] std::string ExecutablePath(uint32_t pid) const;

        /// How many logical processors the machine's active processor group holds. Zero when the
        /// query is unavailable.
        [[nodiscard]] static uint32_t ActiveProcessorCount() noexcept;

        /// How many processor groups the machine presents.
        [[nodiscard]] static uint32_t ActiveProcessorGroupCount() noexcept;
    };
}
