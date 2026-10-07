// Process enumeration on Windows.
//
// Only the Windows implementation lives here; the data types it produces are in
// Platform/ProcessTypes.h so the Domain layer never includes this header.
#pragma once

#include "Platform/ProcessTypes.h"
#include "Platform/Result.h"

namespace tmpp::platform
{
    /**
     * @brief Reads process counters from Windows.
     *
     * Stateless and cheap to construct. Enumerate is the only operation: it takes
     * one bulk snapshot for the whole system rather than opening a handle per
     * process, which keeps the cost roughly independent of the process count.
     */
    class WindowsProcessProbe
    {
    public:
        WindowsProcessProbe();

        /**
         * @brief Enumerates all processes in a single bulk snapshot.
         *
         * Uses one NtQuerySystemInformation(SystemProcessInformation) call for the
         * whole system, supplying CPU times, memory, I/O and handle/thread counts
         * for every process.
         */
        [[nodiscard]] Result<ProcessSnapshot> Enumerate() const;

        /**
         * @brief Reads the size of the memory compression store from a snapshot.
         *
         * Windows compresses memory into a working set of its own, held by a process named
         * "Memory Compression" that has no image on disk. That process's residency is what Windows
         * Task Manager reports as compressed memory, which makes it the figure the composition
         * strip marks with its hatch.
         *
         * Taken from the snapshot the process list is built from rather than read with a query of
         * its own: a second bulk enumeration for a single number would double the cost of every
         * sample (docs/METRICS.md, constraint P-001).
         *
         * @param snapshot A snapshot from Enumerate.
         * @return The compression process's working set, or zero when there is no such process,
         *         which is what a machine with memory compression disabled reports.
         */
        [[nodiscard]] static uint64_t CompressedMemoryBytes(ProcessSnapshot const& snapshot) noexcept;

        [[nodiscard]] ProcessCapabilities Capabilities() const noexcept { return m_capabilities; }

    private:
        ProcessCapabilities m_capabilities;
    };
}
