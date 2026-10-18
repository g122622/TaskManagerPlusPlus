// Measures what selecting a process costs.
//
// Selecting a row reads that process's settings and its executable path. Both open the process, and
// both run on the UI thread inside the row's pointer handler, so a slow read is not a slow figure --
// it is a stalled window. A timing test is the only way to see it, because a slow read still returns
// the right answer.
//
// The interesting part is the tail rather than the mean: opening a handle is microseconds for most
// processes and can be orders of magnitude slower for one that is protected, paging, or shutting
// down. Reporting the worst five by name is what makes that visible.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "Platform/Windows/WindowsProcessProbe.h"
#include "Platform/Windows/WindowsProcessTuning.h"

namespace tmpp::platform::test
{
    namespace
    {
        struct Reading
        {
            uint32_t pid{0};
            std::string name;
            double readMs{0.0};
            double pathMs{0.0};

            [[nodiscard]] double Total() const noexcept { return readMs + pathMs; }
        };

        [[nodiscard]] double _millisSince(std::chrono::steady_clock::time_point start)
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        [[nodiscard]] double _percentile(std::vector<double> sorted, double fraction)
        {
            if (sorted.empty())
            {
                return 0.0;
            }

            std::sort(sorted.begin(), sorted.end());
            auto const index = static_cast<size_t>(fraction * static_cast<double>(sorted.size() - 1));
            return sorted[index];
        }
    }

    TEST(ProcessTuningTiming, SelectingAProcessIsFastForTheWholeMachine)
    {
        WindowsProcessProbe probe;
        auto const snapshot = probe.Enumerate();
        ASSERT_TRUE(snapshot.Success());

        WindowsProcessTuning tuning;

        // A warm-up pass, so the first call's one-off costs -- the ntdll lookups, the first heap
        // growth of the path buffer -- are not attributed to whichever process happened to be first.
        for (ProcessInfo const& process : snapshot.Value().processes)
        {
            (void)tuning.Read(process.identity.pid);
        }

        std::vector<Reading> readings;
        readings.reserve(snapshot.Value().processes.size());

        for (ProcessInfo const& process : snapshot.Value().processes)
        {
            Reading reading;
            reading.pid = process.identity.pid;
            reading.name = process.imageName.empty() ? "<no image>" : process.imageName;

            auto const readStart = std::chrono::steady_clock::now();
            (void)tuning.Read(reading.pid);
            reading.readMs = _millisSince(readStart);

            auto const pathStart = std::chrono::steady_clock::now();
            (void)tuning.ExecutablePath(reading.pid);
            reading.pathMs = _millisSince(pathStart);

            readings.push_back(std::move(reading));
        }

        double total = 0.0;
        std::vector<double> totals;
        totals.reserve(readings.size());
        for (Reading const& reading : readings)
        {
            total += reading.Total();
            totals.push_back(reading.Total());
        }

        double const roundMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                     std::chrono::steady_clock::now())
                .count();
        (void)roundMs;

        std::vector<Reading> worst = readings;
        std::sort(worst.begin(), worst.end(), [](Reading const& left, Reading const& right) {
            return left.Total() > right.Total();
        });

        std::printf("\n--- one selection, %zu processes ---\n", readings.size());
        std::printf("mean per process %8.3f ms\n", total / static_cast<double>(readings.size()));
        std::printf("p95              %8.3f ms\n", _percentile(totals, 0.95));
        std::printf("p99              %8.3f ms\n", _percentile(totals, 0.99));
        std::printf("worst five:\n");
        for (size_t i = 0; i < (std::min)(size_t{5}, worst.size()); ++i)
        {
            std::printf("  %8.3f ms  (read %7.3f, path %7.3f)  pid %-6u %s\n",
                        worst[i].Total(),
                        worst[i].readMs,
                        worst[i].pathMs,
                        worst[i].pid,
                        worst[i].name.c_str());
        }

        double const p95 = _percentile(totals, 0.95);

        // A frame is 16 ms. The mean is what a user feels while scanning the list, and the p95 is what
        // they feel as an occasional hitch; the worst case is deliberately not asserted on, because a
        // single unresponsive process on a developer's machine must not fail the suite.
        EXPECT_LT(total / static_cast<double>(readings.size()), 1.0)
            << "the mean selection cost must be well inside a frame";
        EXPECT_LT(p95, 16.0) << "95 per cent of selections must fit inside a frame";
    }
}
