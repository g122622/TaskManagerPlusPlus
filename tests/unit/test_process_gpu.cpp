// Diagnostic for the per-process GPU reader.
//
// The GPU Engine counters are published per engine per process, and the instance names are the only
// place the owning process id appears. This prints what the reader derives from them so the figures can
// be compared with what a GPU-bound application should show: an idle desktop reports almost nothing, and
// a process running a 3D workload should stand out.
#include <gtest/gtest.h>

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "Platform/Windows/HardwareCounterProbe.h"

namespace tmpp::platform::test
{
    TEST(ProcessGpuDiagnostic, PrintsEachProcessGpuShare)
    {
        HardwareCounterProbe probe;

        // Two rounds, because the engine counters are rates and the first collection establishes the
        // baseline rather than producing a value.
        auto const first = probe.ReadGpu();
        ASSERT_TRUE(first.Success()) << first.GetError().Message();

        Sleep(1000);

        auto const second = probe.ReadGpu();
        ASSERT_TRUE(second.Success()) << second.GetError().Message();

        std::printf("\n--- per-process GPU ---\n");
        std::printf("adapter total: %.1f%%, available=%s\n",
                    second.Value().utilizationPercent,
                    second.Value().available ? "yes" : "no");

        std::map<uint32_t, double> perProcess;
        probe.ReadProcessGpu(perProcess);

        std::printf("processes reporting any GPU use: %zu\n", perProcess.size());

        // The largest few, which is what the list's GPU column would show at the top of a descending sort.
        std::vector<std::pair<uint32_t, double>> ranked(perProcess.begin(), perProcess.end());
        std::sort(ranked.begin(), ranked.end(), [](auto const& a, auto const& b) { return a.second > b.second; });

        for (size_t i = 0; i < ranked.size() && i < 8; ++i)
        {
            std::printf("  pid %-7u %6.2f%%\n", ranked[i].first, ranked[i].second);
        }

        // The adapter's total is the busiest engine across every process; no single process can exceed it
        // by more than the rounding. A process figure above the adapter's would mean the reduction is
        // adding engines that should have stayed apart.
        double highest = 0.0;
        for (auto const& entry : perProcess)
        {
            highest = (std::max)(highest, entry.second);
            EXPECT_GE(entry.second, 0.0);
            EXPECT_LE(entry.second, 100.0) << "pid " << entry.first << " reported above 100 percent";
        }

        if (second.Value().available && highest > 0.0)
        {
            EXPECT_LE(highest, second.Value().utilizationPercent + 1.0)
                << "a process reported more GPU use than the whole adapter";
        }
    }

    TEST(ProcessGpuDiagnostic, ReportsNothingWhenTheCountersAreUnavailable)
    {
        // The reader must clear its output rather than leaving a previous caller's values in place: the
        // column would otherwise keep showing a process's last reading after it stopped using the GPU.
        HardwareCounterProbe probe;

        std::map<uint32_t, double> perProcess{{1234u, 50.0}};
        probe.ReadProcessGpu(perProcess);

        // With no ReadGpu before it there is no collected sample, so nothing is reported.
        EXPECT_TRUE(perProcess.empty()) << "the reader left stale values in the output";
    }
}
