#include "NtdllApi.h"

#include <cstdlib>
#include <new>

namespace tmpp::platform::nt
{
    NtQuerySystemInformationFn QuerySystemInformation() noexcept
    {
        // ntdll.dll is mapped into every user-mode process, so GetModuleHandle
        // cannot fail here in practice; treat a null module as "unavailable".
        static NtQuerySystemInformationFn const cached = []() noexcept -> NtQuerySystemInformationFn {
            HMODULE const ntdll = GetModuleHandleW(L"ntdll.dll");
            if (ntdll == nullptr)
            {
                return nullptr;
            }
            return reinterpret_cast<NtQuerySystemInformationFn>(
                GetProcAddress(ntdll, "NtQuerySystemInformation"));
        }();

        return cached;
    }

    bool QueryWithGrowingBuffer(SYSTEM_INFORMATION_CLASS infoClass,
                                size_t initialBytes,
                                size_t maxBytes,
                                void** outBuffer,
                                LONG* outStatus)
    {
        *outBuffer = nullptr;
        *outStatus = STATUS_SUCCESS;

        auto const query = QuerySystemInformation();
        if (query == nullptr)
        {
            *outStatus = STATUS_SUCCESS; // No native API: caller reports the metric as unavailable.
            return false;
        }

        size_t capacity = initialBytes;
        for (;;)
        {
            // Grow in steps so a burst of short-lived processes cannot force many
            // reallocations, but keep the first attempt small.
            void* buffer = std::malloc(capacity);
            if (buffer == nullptr)
            {
                return false;
            }

            ULONG returned = 0;
            LONG const status = query(infoClass, buffer, static_cast<ULONG>(capacity), &returned);

            if (status == STATUS_SUCCESS || status == STATUS_BUFFER_OVERFLOW)
            {
                *outBuffer = buffer;
                *outStatus = status;
                return true;
            }

            std::free(buffer);

            if (status != STATUS_INFO_LENGTH_MISMATCH && status != STATUS_BUFFER_TOO_SMALL)
            {
                // A genuine failure; do not keep retrying.
                *outStatus = status;
                return false;
            }

            // The API reports the required size in `returned` when it can. Fall back
            // to doubling when it does not, and stop at the caller's ceiling.
            size_t const next = (returned > capacity) ? static_cast<size_t>(returned) + (returned / 4) : capacity * 2;
            if (next <= capacity || next > maxBytes)
            {
                *outStatus = status;
                return false;
            }
            capacity = next;
        }
    }
}
