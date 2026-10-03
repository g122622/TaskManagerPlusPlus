// Dynamic loading of the ntdll entry points the probes need.
//
// NtQuerySystemInformation and friends are the documented-stable but not
// officially-published native API. Linking them statically would need the WDK
// headers and would tie the binary to a specific import library, so they are
// resolved at runtime from ntdll.dll (always already loaded in every process).
#pragma once

#include <windows.h>

// NTAPI and the SYSTEM_INFORMATION_CLASS enum come from winternl.h. Including it
// here keeps this header self-contained for callers that only need the query
// helper.
#include <winternl.h>

#include <cstdint>

namespace tmpp::platform::nt
{
    // ------------------------------------------------------------------------
    // Native NTSTATUS values used by the probes.
    // ------------------------------------------------------------------------
    inline constexpr LONG STATUS_SUCCESS = 0;
    inline constexpr LONG STATUS_INFO_LENGTH_MISMATCH = static_cast<LONG>(0xC0000004);
    inline constexpr LONG STATUS_BUFFER_TOO_SMALL = static_cast<LONG>(0xC0000023);
    inline constexpr LONG STATUS_BUFFER_OVERFLOW = static_cast<LONG>(0x80000005);

    /**
     * @brief Signature of NtQuerySystemInformation.
     */
    using NtQuerySystemInformationFn = LONG(NTAPI*)(SYSTEM_INFORMATION_CLASS systemInformationClass,
                                                    PVOID systemInformation,
                                                    ULONG systemInformationLength,
                                                    PULONG returnLength);

    /**
     * @brief Returns the NtQuerySystemInformation pointer, or nullptr if unavailable.
     *
     * The lookup happens once and is cached. A nullptr result is not an error to
     * log repeatedly: ntdll is present in every process, so this only fails on an
     * unsupported system.
     */
    [[nodiscard]] NtQuerySystemInformationFn QuerySystemInformation() noexcept;

    /**
     * @brief Queries system information into a caller-owned buffer, growing it on demand.
     *
     * Starts at @p initialBytes and retries with a larger buffer while the API
     * reports STATUS_INFO_LENGTH_MISMATCH, which is how this API signals that the
     * snapshot did not fit.
     *
     * @param infoClass Information class to query.
     * @param initialBytes First buffer size to try.
     * @param maxBytes Upper bound on growth, to bound memory use on a hostile system.
     * @param outBuffer Receives the buffer that satisfied the query.
     * @param outStatus Receives the final NTSTATUS.
     * @return true when the query succeeded.
     */
    [[nodiscard]] bool QueryWithGrowingBuffer(SYSTEM_INFORMATION_CLASS infoClass,
                                              size_t initialBytes,
                                              size_t maxBytes,
                                              void** outBuffer,
                                              LONG* outStatus);
}
