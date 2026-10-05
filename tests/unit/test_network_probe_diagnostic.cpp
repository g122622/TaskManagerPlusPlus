// Diagnostics for the network probe's cost.
//
// Not assertions about behaviour: they measure and print, so the cause of a slow network read can be seen
// rather than guessed at. They are kept because the cost depends on the machine's adapter set -- a machine
// with more virtual adapters pays more -- and the next person to see a slow sampler needs the breakdown,
// not a threshold.
//
// What the measurements established:
//
//   * GetIfTable2, the enumeration, costs between half a second and four seconds and is called once per
//     probe. It asks every adapter's driver, including the ones that are not present.
//   * GetIfEntry2, the per-sample poll, is fast for all but one adapter on this machine.
//   * Exactly one adapter blocks: an NDIS internet-sharing device, a physical interface that is up and
//     media-connected. Its query stalls for about two and a half seconds, intermittently -- over a hundred
//     and twenty rounds it was the only adapter of fifteen to exceed a millisecond, and it stalled on about
//     four of them.
//   * The stall is not caused by the disk or the GPU probe. An earlier run appeared to show that, because
//     the phases ran in sequence and the later ones happened to fall in a bad window; repeating the first
//     phase at the end produced a slow result too, which retired the conclusion.
//
// So the cost is one adapter's driver, and the per-interface measurement has to run enough rounds to catch
// it: a handful finds nothing, because the stall is rare per call and frequent per sample.
#include <gtest/gtest.h>

#include <winsock2.h>
#include <ws2ipdef.h>
#include <windows.h>
#include <iphlpapi.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Platform/Windows/HardwareCounterProbe.h"

namespace tmpp::platform::test
{
    namespace
    {
        [[nodiscard]] double _millisSince(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        [[nodiscard]] char const* _operStatusName(IF_OPER_STATUS status)
        {
            switch (status)
            {
                case IfOperStatusUp:
                    return "Up";
                case IfOperStatusDown:
                    return "Down";
                case IfOperStatusTesting:
                    return "Testing";
                case IfOperStatusUnknown:
                    return "Unknown";
                case IfOperStatusDormant:
                    return "Dormant";
                case IfOperStatusNotPresent:
                    return "NotPresent";
                case IfOperStatusLowerLayerDown:
                    return "LowerLayerDown";
                default:
                    return "?";
            }
        }

        /// Per-interface totals over many rounds, so a rare stall shows up in the maximum.
        struct AdapterTiming
        {
            uint32_t index{0};
            ULONG type{0};
            IF_OPER_STATUS operStatus{IfOperStatusUnknown};
            NET_IF_MEDIA_CONNECT_STATE mediaState{MediaConnectStateUnknown};
            bool hardware{false};
            bool filter{false};
            std::wstring description;
            double maxMillis{0.0};
            double totalMillis{0.0};
        };

        [[nodiscard]] char const* _mediaStateName(NET_IF_MEDIA_CONNECT_STATE state)
        {
            switch (state)
            {
                case MediaConnectStateConnected:
                    return "Connected";
                case MediaConnectStateDisconnected:
                    return "Disconnected";
                default:
                    return "Unknown";
            }
        }
    }

    TEST(NetworkProbeDiagnostic, FindsTheAdapterThatBlocks)
    {
        PMIB_IF_TABLE2 table = nullptr;
        auto const tableStart = std::chrono::steady_clock::now();
        ULONG const tableResult = GetIfTable2(&table);
        double const tableMillis = _millisSince(tableStart);

        // Cast because NO_ERROR is a signed zero and the result is unsigned, and comparing the two draws a
        // warning from inside the assertion macro rather than from here.
        ASSERT_EQ(tableResult, static_cast<ULONG>(NO_ERROR));
        ASSERT_NE(table, nullptr);
        auto const cleanup = std::unique_ptr<MIB_IF_TABLE2, decltype(&FreeMibTable)>(table, &FreeMibTable);

        std::vector<AdapterTiming> timings;
        for (ULONG i = 0; i < table->NumEntries; ++i)
        {
            MIB_IF_ROW2 const& row = table->Table[i];

            // The same three filters the probe applies when it builds its list, so what is timed here is
            // the set the probe actually polls rather than the whole table. A stall in an interface the
            // probe does not poll would be a false lead.
            if (row.OperStatus == IfOperStatusNotPresent)
            {
                continue;
            }
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.Type == IF_TYPE_TUNNEL)
            {
                continue;
            }
            if (row.InterfaceAndOperStatusFlags.FilterInterface != 0)
            {
                continue;
            }

            AdapterTiming timing;
            timing.index = static_cast<uint32_t>(row.InterfaceIndex);
            timing.type = row.Type;
            timing.operStatus = row.OperStatus;
            timing.hardware = row.InterfaceAndOperStatusFlags.HardwareInterface != 0;
            timing.filter = row.InterfaceAndOperStatusFlags.FilterInterface != 0;
            timing.mediaState = row.MediaConnectState;
            timing.description = row.Description;
            timings.push_back(std::move(timing));
        }

        std::printf("\n=== adapter stall hunt (the probe's own polling set) ===\n");
        std::printf("GetIfTable2: %.2f ms, %lu entries, %zu polled\n",
                    tableMillis,
                    table->NumEntries,
                    timings.size());

        // Enough rounds that a stall which happens once in a few dozen calls is seen. The probe polls this
        // whole set on every sample, so one adapter stalling is a stall for the whole sample.
        constexpr int ROUNDS = 120;

        for (int round = 0; round < ROUNDS; ++round)
        {
            double roundTotal = 0.0;

            for (AdapterTiming& timing : timings)
            {
                MIB_IF_ROW2 row{};
                row.InterfaceIndex = timing.index;

                auto const start = std::chrono::steady_clock::now();
                (void)GetIfEntry2(&row);
                double const elapsed = _millisSince(start);

                timing.maxMillis = (std::max)(timing.maxMillis, elapsed);
                timing.totalMillis += elapsed;
                roundTotal += elapsed;
            }

            // A round that took real time says the stall is in the set; the per-adapter maxima then say
            // which one. Printed because a round total that is fast every time would mean the stall is not
            // in this loop at all.
            if (roundTotal > 50.0)
            {
                std::printf("round %3d took %8.2f ms\n", round, roundTotal);
            }
        }

        std::sort(timings.begin(), timings.end(), [](AdapterTiming const& left, AdapterTiming const& right) {
            return left.maxMillis > right.maxMillis;
        });

        std::printf("\n%-7s %-14s %-13s %-5s %-8s %-6s %11s %10s  %s\n",
                    "index", "operStatus", "media", "type", "hardware", "filter", "maxMs", "meanMs", "description");

        // Only the ones that ever took real time. A list of seventy rows at a twentieth of a millisecond
        // each buries the two that matter.
        constexpr double THRESHOLD_MS = 1.0;
        for (AdapterTiming const& timing : timings)
        {
            if (timing.maxMillis < THRESHOLD_MS)
            {
                continue;
            }

            std::printf("%-7u %-14s %-13s %-5lu %-8s %-6s %11.2f %10.3f  %ls\n",
                        timing.index,
                        _operStatusName(timing.operStatus),
                        _mediaStateName(timing.mediaState),
                        timing.type,
                        timing.hardware ? "yes" : "no",
                        timing.filter ? "yes" : "no",
                        timing.maxMillis,
                        timing.totalMillis / static_cast<double>(ROUNDS),
                        timing.description.c_str());
        }

        size_t const over = static_cast<size_t>(std::count_if(
            timings.begin(), timings.end(),
            [](AdapterTiming const& timing) { return timing.maxMillis >= THRESHOLD_MS; }));

        std::printf("\n(%zu of %zu adapters ever exceeded %.1f ms over %d rounds)\n",
                    over,
                    timings.size(),
                    THRESHOLD_MS,
                    ROUNDS);

        SUCCEED();
    }
}
