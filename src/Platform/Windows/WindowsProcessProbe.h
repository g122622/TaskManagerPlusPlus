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

        [[nodiscard]] ProcessCapabilities Capabilities() const noexcept { return m_capabilities; }

    private:
        ProcessCapabilities m_capabilities;
    };
}
