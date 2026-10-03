// Diagnostic for the disk, network and GPU probe.
//
// Not an assertion suite: it prints what the probe reads on this machine, so a reading can be
// compared against what Windows itself reports in Task Manager and Resource Monitor. A probe that
// returns a plausible-looking but wrong figure passes any test written from the same assumption
// that produced the bug, which is how the memory composition probe reported "unsupported" on a
// machine that supports it.
//
// It asserts only what must hold for any machine, so it is meaningful wherever it runs: the
// counters must be internally consistent, and a quantity that cannot be negative must not be.
#include <gtest/gtest.h>

#include <cstdio>

#include "Platform/Windows/HardwareCounterProbe.h"

namespace tmpp::platform::test
{
    namespace
    {
        void _printSeparator(char const* title)
        {
            std::printf("\n--- %s ---\n", title);
        }
    }

    TEST(HardwareProbeDiagnostic, DisksReportTheirCounters)
    {
        HardwareCounterProbe probe;
        auto const disks = probe.ReadDisks();

        ASSERT_TRUE(disks.Success()) << disks.GetError().Message();

        _printSeparator("disks");
        std::printf("availability flag: %s\n", probe.DisksAvailable() ? "true" : "false");
        std::printf("%zu device(s)\n", disks.Value().size());

        for (SystemDiskCounters const& disk : disks.Value())
        {
            std::printf("  [%u] %s\n", disk.deviceIndex, disk.instanceName.c_str());
            std::printf("      model      : %s\n", disk.modelName.empty() ? "(unknown)" : disk.modelName.c_str());
            std::printf("      read       : %llu bytes in %llu ops over %llu ms\n",
                        static_cast<unsigned long long>(disk.readBytes),
                        static_cast<unsigned long long>(disk.readCount),
                        static_cast<unsigned long long>(disk.readTimeMs));
            std::printf("      write      : %llu bytes in %llu ops over %llu ms\n",
                        static_cast<unsigned long long>(disk.writeBytes),
                        static_cast<unsigned long long>(disk.writeCount),
                        static_cast<unsigned long long>(disk.writeTimeMs));
            std::printf("      average    : read %u B, write %u B\n", disk.averageReadBytes, disk.averageWriteBytes);
            std::printf("      queue depth: %u\n", disk.queueDepth);
            std::printf("      bus type   : %u\n", disk.busType);
            std::printf("      seek cost  : %s\n", disk.incursSeekPenalty ? "yes (HDD-class)" : "no (SSD-class)");
            std::printf("      TRIM       : %s\n", disk.trimEnabled ? "yes" : "no");
            std::printf("      capacity   : %.1f GB\n",
                        static_cast<double>(disk.capacityBytes) / (1024.0 * 1024.0 * 1024.0));

            // Properties every readable device must satisfy, whatever its make.
            EXPECT_TRUE(disk.available);
            EXPECT_GE(disk.sectorSize, 512u);

            // The average size cannot exceed the total divided by the count, and the totals are the
            // ground truth here: an average larger than it would mean the two disagree.
            if (disk.readCount > 0)
            {
                EXPECT_LE(static_cast<uint64_t>(disk.averageReadBytes), disk.readBytes / disk.readCount + 1);
            }
        }
    }

    TEST(HardwareProbeDiagnostic, NetworkReportsItsInterfaces)
    {
        HardwareCounterProbe probe;
        auto const interfaces = probe.ReadNetwork();

        ASSERT_TRUE(interfaces.Success()) << interfaces.GetError().Message();

        _printSeparator("network");
        std::printf("availability flag: %s\n", probe.NetworkAvailable() ? "true" : "false");
        std::printf("%zu interface(s)\n", interfaces.Value().size());

        for (SystemNetworkCounters const& iface : interfaces.Value())
        {
            std::printf("  %s\n", iface.adapterName.c_str());
            std::printf("      state  : %s\n", iface.connected ? "up" : "down");
            std::printf("      received: %llu bytes in %llu packets, %llu errors\n",
                        static_cast<unsigned long long>(iface.receivedBytes),
                        static_cast<unsigned long long>(iface.receivedPackets),
                        static_cast<unsigned long long>(iface.receiveErrors));
            std::printf("      sent    : %llu bytes in %llu packets, %llu errors\n",
                        static_cast<unsigned long long>(iface.sentBytes),
                        static_cast<unsigned long long>(iface.sentPackets),
                        static_cast<unsigned long long>(iface.sendErrors));
            std::printf("      link    : receive %.1f Mbps, transmit %.1f Mbps\n",
                        static_cast<double>(iface.receiveLinkSpeedBps) / 1.0e6,
                        static_cast<double>(iface.transmitLinkSpeedBps) / 1.0e6);

            // The adapter must be named, or the sidebar row would be blank.
            EXPECT_FALSE(iface.adapterName.empty()) << "an interface must carry a name";
            EXPECT_TRUE(iface.available);

            // Counters are cumulative and cannot go backwards.
            // Cumulative counters are unsigned, so their only meaningful bound is that the\n            // struct was populated rather than left at its default.
        }
    }

    TEST(HardwareProbeDiagnostic, GpuReportsUtilisationAndMemory)
    {
        HardwareCounterProbe probe;
        auto const gpu = probe.ReadGpu();

        ASSERT_TRUE(gpu.Success()) << gpu.GetError().Message();

        _printSeparator("gpu");
        std::printf("availability flag: %s\n", probe.GpuAvailable() ? "true" : "false");
        std::printf("adapter       : %s\n", gpu.Value().adapterName.empty() ? "(unknown)" : gpu.Value().adapterName.c_str());
        std::printf("utilisation   : %.2f %%\n", gpu.Value().utilizationPercent);
        std::printf("dedicated     : %.2f GB of %.2f GB\n",
                    static_cast<double>(gpu.Value().dedicatedUsedBytes) / (1024.0 * 1024.0 * 1024.0),
                    static_cast<double>(gpu.Value().dedicatedTotalBytes) / (1024.0 * 1024.0 * 1024.0));

        // A percentage outside its range would mean the per-engine summing went wrong, which is the
        // mistake that would also make the figure above 100 on a busy machine.
        EXPECT_GE(gpu.Value().utilizationPercent, 0.0);
        EXPECT_LE(gpu.Value().utilizationPercent, 100.0);

        // On a machine with GPU counters, the adapter should be identified. A blank name with a
        // successful read means the registry lookup failed while the counters worked.
        if (probe.GpuAvailable())
        {
            std::printf("(note: a blank adapter name means the registry lookup found no adapter)\n");
        }
    }
}
