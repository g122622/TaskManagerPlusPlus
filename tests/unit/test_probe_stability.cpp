// Measures how often a hardware read comes back empty.
//
// An empty device list is reported as a successful read, so nothing upstream can tell it apart from a
// machine with no disks. That matters because the sidebar builds one row per device: a single empty
// sample makes every disk and network row disappear and come back, which is what a user sees as the
// items flickering out of the list.
//
// This counts the occurrences over many reads so the frequency is known rather than assumed.
#include <gtest/gtest.h>

#include <cstdio>

#include "Platform/Windows/HardwareCounterProbe.h"

namespace tmpp::platform::test
{
    TEST(HardwareProbeStability, ReadsRarelyComeBackEmpty)
    {
        HardwareCounterProbe probe;

        constexpr int ROUNDS = 200;
        int emptyDisks = 0;
        int emptyNetworks = 0;
        int failedDisks = 0;
        int failedNetworks = 0;
        size_t firstDiskCount = 0;
        size_t firstNetworkCount = 0;

        for (int i = 0; i < ROUNDS; ++i)
        {
            auto const disks = probe.ReadDisks();
            if (!disks.Success())
            {
                ++failedDisks;
            }
            else if (disks.Value().empty())
            {
                ++emptyDisks;
            }
            else if (firstDiskCount == 0)
            {
                firstDiskCount = disks.Value().size();
            }

            auto const networks = probe.ReadNetwork();
            if (!networks.Success())
            {
                ++failedNetworks;
            }
            else if (networks.Value().empty())
            {
                ++emptyNetworks;
            }
            else if (firstNetworkCount == 0)
            {
                firstNetworkCount = networks.Value().size();
            }
        }

        std::printf("\n--- hardware read stability over %d rounds ---\n", ROUNDS);
        std::printf("disks    : %zu devices, %d empty reads, %d failed reads\n",
                    firstDiskCount,
                    emptyDisks,
                    failedDisks);
        std::printf("networks : %zu interfaces, %d empty reads, %d failed reads\n",
                    firstNetworkCount,
                    emptyNetworks,
                    failedNetworks);

        // The devices are physically present for the whole run, so any empty read is a failure to
        // observe rather than an observation of nothing.
        EXPECT_EQ(emptyDisks, 0) << "a disk read came back empty while the disks are present";
        EXPECT_EQ(emptyNetworks, 0) << "a network read came back empty while an adapter is present";
        EXPECT_EQ(failedDisks, 0);
        EXPECT_EQ(failedNetworks, 0);
    }
}
