// Compares the ways of reading network counters, to find one that fits the sampling interval.
//
// GetIfTable2 is the obvious call, and it is fast on most machines but takes over a second on one
// with many virtual adapters -- it queries every adapter's driver, including the ones that are not
// present. A probe that slow does not merely delay one figure: it makes the sampler's interval a
// fiction. Timing is the only way to see it, because the slow call returns perfectly good data.
#include <gtest/gtest.h>

// The include order is required: netioapi.h is reached through iphlpapi.h after the Winsock headers,
// and winsock2.h must precede windows.h.
#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>

#include <iphlpapi.h>
#include <pdh.h>

#include <chrono>
#include <cstdio>
#include <vector>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "pdh.lib")

namespace tmpp::platform::test
{
    namespace
    {
        template <typename TCallable>
        [[nodiscard]] double _timeMs(TCallable&& call, int rounds)
        {
            auto const start = std::chrono::steady_clock::now();
            for (int i = 0; i < rounds; ++i)
            {
                call();
            }
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() /
                   static_cast<double>(rounds);
        }

        constexpr int ROUNDS = 5;
    }

    TEST(NetworkApiBenchmark, ComparesTheAvailableCalls)
    {
        std::printf("\n--- network counter APIs, mean of %d ---\n", ROUNDS);

        // --- GetIfTable2: the call the probe currently uses ---
        {
            size_t entries = 0;
            double const mean = _timeMs(
                [&entries] {
                    PMIB_IF_TABLE2 table = nullptr;
                    if (GetIfTable2(&table) == NO_ERROR && table != nullptr)
                    {
                        entries = table->NumEntries;
                        FreeMibTable(table);
                    }
                },
                ROUNDS);

            std::printf("GetIfTable2              %8.2f ms  (%zu entries)\n", mean, entries);
        }

        // --- GetNumberOfInterfaces then GetIfEntry2 per index ---
        //
        // GetIfEntry2 asks for one interface at a time. If the cost of GetIfTable2 is the per-adapter
        // driver query, this has the same cost; if it is the enumeration itself, this avoids it.
        {
            size_t entries = 0;
            double const mean = _timeMs(
                [&entries] {
                    ULONG count = 0;
                    if (GetNumberOfInterfaces(&count) != NO_ERROR)
                    {
                        return;
                    }

                    size_t reported = 0;
                    for (ULONG index = 0; index < count; ++index)
                    {
                        MIB_IF_ROW2 row{};
                        row.InterfaceIndex = index;
                        if (GetIfEntry2(&row) == NO_ERROR)
                        {
                            ++reported;
                        }
                    }
                    entries = reported;
                },
                ROUNDS);

            std::printf("GetIfEntry2 per index    %8.2f ms  (%zu entries)\n", mean, entries);
        }

        // --- GetAdaptersAddresses ---
        //
        // The IP Helper adapter list. It reports octet counts per unicast address rather than per
        // interface, which is why the reference did not use it, but it is worth measuring.
        {
            size_t entries = 0;
            double const mean = _timeMs(
                [&entries] {
                    ULONG size = 16 * 1024;
                    std::vector<uint8_t> buffer(size);

                    ULONG result = GetAdaptersAddresses(AF_UNSPEC,
                                                        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                                            GAA_FLAG_SKIP_DNS_SERVER,
                                                        nullptr,
                                                        reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                                        &size);

                    if (result == ERROR_BUFFER_OVERFLOW)
                    {
                        buffer.resize(size);
                        result = GetAdaptersAddresses(AF_UNSPEC,
                                                      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST |
                                                          GAA_FLAG_SKIP_DNS_SERVER,
                                                      nullptr,
                                                      reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),
                                                      &size);
                    }

                    if (result != NO_ERROR)
                    {
                        return;
                    }

                    size_t reported = 0;
                    for (auto const* adapter = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
                         adapter != nullptr;
                         adapter = adapter->Next)
                    {
                        ++reported;
                    }
                    entries = reported;
                },
                ROUNDS);

            std::printf("GetAdaptersAddresses     %8.2f ms  (%zu entries)\n", mean, entries);
        }

        // --- PDH Network Interface counters ---
        //
        // The sampler already runs PDH queries for the GPU, and those cost single-digit
        // milliseconds. These counters are rates rather than cumulative totals, which is a real
        // difference in meaning, so the figure here is only about speed.
        {
            size_t entries = 0;
            PDH_HQUERY query = nullptr;
            PDH_HCOUNTER counter = nullptr;

            if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS &&
                PdhAddEnglishCounterW(query, L"\\Network Interface(*)\\Bytes Received/sec", 0, &counter) == ERROR_SUCCESS)
            {
                PdhCollectQueryData(query);

                double const mean = _timeMs(
                    [&] {
                        PdhCollectQueryData(query);

                        DWORD bufferSize = 0;
                        DWORD itemCount = 0;
                        PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr);
                        if (bufferSize == 0)
                        {
                            return;
                        }

                        std::vector<uint8_t> buffer(bufferSize);
                        if (PdhGetFormattedCounterArrayW(counter,
                                                         PDH_FMT_DOUBLE,
                                                         &bufferSize,
                                                         &itemCount,
                                                         reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(
                                                             buffer.data())) == ERROR_SUCCESS)
                        {
                            entries = itemCount;
                        }
                    },
                    ROUNDS);

                std::printf("PDH Network Interface    %8.2f ms  (%zu instances)\n", mean, entries);
                PdhCloseQuery(query);
            }
            else
            {
                std::printf("PDH Network Interface    unavailable\n");
            }
        }

        // The assertion is deliberately generous: this prints the comparison, and the useful
        // outcome is the table above rather than a pass or fail.
        SUCCEED();
    }
}
