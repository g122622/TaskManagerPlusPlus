// Tests for deriving disk and network rates from cumulative counters.
//
// The derivation is the part that can be subtly wrong without looking wrong: a rate computed against
// the wrong interval, or one that survives a counter reset, still produces a number of the right
// magnitude. What it will not do is match what the machine is actually doing, and that is only
// visible by comparing against a known interval.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "Domain/SystemModel.h"

#include <windows.h>

namespace tmpp::domain::test
{
    namespace
    {
        constexpr uint32_t INTERVAL_MS = 1000;

        /// Tick counts derived from the real counter frequency.
        ///
        /// A captured timestamp is a QueryPerformanceCounter value, not a millisecond count, so a
        /// synthetic interval has to be expressed in the same units. Adding a millisecond literal to
        /// one produces a microsecond interval and a rate that is off by orders of magnitude, which
        /// is precisely the mistake these tests exist to catch.
        [[nodiscard]] uint64_t _ticksPerSecond() noexcept
        {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency(&frequency);
            return static_cast<uint64_t>(frequency.QuadPart);
        }

        /// A base timestamp, well clear of zero so a baseline comparison is meaningful.
        uint64_t const T0 = 1000000;

        /// T0 plus a whole number of milliseconds, in counter ticks.
        [[nodiscard]] uint64_t _afterMs(uint64_t millis) noexcept
        {
            return T0 + ((_ticksPerSecond() * millis) / 1000);
        }
        constexpr uint32_t HISTORY_SECONDS = 60;

        /// Builds a disk reading. The fifth parameter is idle time, which is what active time is
        /// derived from: read and write service times are reported but not used for it.
        platform::SystemDiskCounters _disk(std::string name,
                                           uint64_t read,
                                           uint64_t write,
                                           uint64_t readMs,
                                           uint64_t writeMs,
                                           uint64_t idleMs = 0)
        {
            platform::SystemDiskCounters disk;
            disk.instanceName = std::move(name);
            disk.readBytes = read;
            disk.writeBytes = write;
            disk.readTimeMs = readMs;
            disk.writeTimeMs = writeMs;
            disk.idleTimeMs = idleMs;
            disk.available = true;
            return disk;
        }

        platform::SystemNetworkCounters _network(std::string name, uint64_t received, uint64_t sent)
        {
            platform::SystemNetworkCounters iface;
            iface.adapterName = std::move(name);
            iface.receivedBytes = received;
            iface.sentBytes = sent;
            iface.receiveLinkSpeedBps = 1'000'000'000;
            iface.transmitLinkSpeedBps = 1'000'000'000;
            iface.connected = true;
            iface.available = true;
            return iface;
        }
    }

    TEST(DiskRateTest, FirstSampleProducesNoRate)
    {
        // A rate needs two readings. Reporting zero would claim the disk was idle, and reporting the
        // cumulative total as a rate would claim it transferred years of data in one second.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        std::vector<platform::SystemDiskCounters> const disks{_disk("0 C:", 1'000'000, 500'000, 100, 50)};
        model.SetHardwareCounters(disks, {}, {}, T0);

        auto const history = model.History();
        ASSERT_FALSE(history.diskReadBytesPerSecond.empty());
        EXPECT_DOUBLE_EQ(history.diskReadBytesPerSecond.back(), 0.0);
        EXPECT_DOUBLE_EQ(history.diskWriteBytesPerSecond.back(), 0.0);
    }

    TEST(DiskRateTest, SecondSampleDividesByTheInterval)
    {
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:", 1'000'000, 500'000, 100, 50)}, {}, {}, T0);

        // One second later the device has moved 2 MB read and 1 MB written. At a two-second interval
        // the same movement must halve the rate, which is what this test pins down.
        model.SetHardwareCounters({_disk("0 C:", 3'000'000, 1'500'000, 200, 100)}, {}, {}, _afterMs(2000));

        auto const history = model.History();
        ASSERT_GE(history.diskReadBytesPerSecond.size(), 2u);
        EXPECT_DOUBLE_EQ(history.diskReadBytesPerSecond.back(), 1'000'000.0);
        EXPECT_DOUBLE_EQ(history.diskWriteBytesPerSecond.back(), 500'000.0);
    }

    TEST(DiskRateTest, ACounterResetDoesNotProduceANegativeOrHugeRate)
    {
        // A device that is replaced or reset restarts its counters. Differencing across that would
        // underflow to an enormous unsigned value, which is the trap the comparison guards against.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:", 10'000'000, 5'000'000, 1000, 500)}, {}, {}, T0);
        model.SetHardwareCounters({_disk("0 C:", 1'000, 500, 1, 1)}, {}, {}, _afterMs(1000));

        auto const history = model.History();
        ASSERT_GE(history.diskReadBytesPerSecond.size(), 2u);

        // The only honest answer for a reset counter is no rate at all.
        EXPECT_DOUBLE_EQ(history.diskReadBytesPerSecond.back(), 0.0);
        EXPECT_DOUBLE_EQ(history.diskWriteBytesPerSecond.back(), 0.0);
        EXPECT_GE(history.diskReadBytesPerSecond.back(), 0.0) << "a rate cannot be negative";
    }

    TEST(DiskRateTest, DevicesAreMatchedByNameNotByPosition)
    {
        // Enumeration order is not guaranteed. Matching by index would attribute one device's traffic
        // to another the moment the order changed, which is a silent error.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:", 1'000'000, 0, 10, 0), _disk("1 D:", 0, 0, 0, 0)}, {}, {}, T0);

        // The same two devices, now in the opposite order, each having advanced by a known amount.
        model.SetHardwareCounters({_disk("1 D:", 4'000'000, 0, 40, 0), _disk("0 C:", 2'000'000, 0, 20, 0)}, {}, {}, _afterMs(1000));

        auto const history = model.History();
        ASSERT_GE(history.diskReadBytesPerSecond.size(), 2u);

        // By name, device 0 advanced 1 MB and device 1 advanced 4 MB, so the aggregate is 5 MB/s.
        // Matching by position would have differenced 1 MB against 4 MB and produced nonsense.
        EXPECT_DOUBLE_EQ(history.diskReadBytesPerSecond.back(), 5'000'000.0);
    }

    TEST(DiskRateTest, ActiveTimeIsTheComplementOfIdleTime)
    {
        // Idle for none of a one-second interval means fully busy; idle for all of it means not busy
        // at all. This is the property the figure rests on.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:", 0, 0, 0, 0, 0)}, {}, {}, T0);
        model.SetHardwareCounters({_disk("0 C:", 0, 0, 0, 0, 0)}, {}, {}, _afterMs(1000));

        SystemView const& busy = model.Latest();
        ASSERT_EQ(busy.disks.size(), 1u);
        EXPECT_DOUBLE_EQ(busy.disks[0].activePercent, 100.0);

        SystemModel idleModel(8, INTERVAL_MS, HISTORY_SECONDS);
        idleModel.SetHardwareCounters({_disk("0 C:", 0, 0, 0, 0, 0)}, {}, {}, T0);
        idleModel.SetHardwareCounters({_disk("0 C:", 0, 0, 0, 0, 1000)}, {}, {}, _afterMs(1000));

        SystemView const& idle = idleModel.Latest();
        ASSERT_EQ(idle.disks.size(), 1u);
        EXPECT_DOUBLE_EQ(idle.disks[0].activePercent, 0.0);
    }

    TEST(DiskRateTest, OverlappingServiceTimeIsNotCountedTwice)
    {
        // The first derivation summed read and write service time, which double-counts a device
        // servicing overlapping requests: both counters advance at once, so the sum exceeds the wall
        // clock and the device is reported as busier than it can be. Idle time is a wall-clock
        // measure and cannot do that.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:", 0, 0, 0, 0, 0)}, {}, {}, T0);

        // In one second the device accumulated 900 ms of read service and 900 ms of write service,
        // overlapping, while being idle for 500 ms. Summing the service times would give 180 percent;
        // the idle counter says half the interval was spent working.
        model.SetHardwareCounters({_disk("0 C:", 0, 0, 900, 900, 500)}, {}, {}, _afterMs(1000));

        SystemView const& latest = model.Latest();
        ASSERT_EQ(latest.disks.size(), 1u);
        EXPECT_DOUBLE_EQ(latest.disks[0].activePercent, 50.0)
            << "the sum of read and write service time would report 180 percent here";
    }

    TEST(NetworkRateTest, RatesAreDerivedFromTheCumulativeCounters)
    {
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({}, {_network("Ethernet", 1'000'000, 200'000)}, {}, T0);
        model.SetHardwareCounters({}, {_network("Ethernet", 3'000'000, 700'000)}, {}, _afterMs(1000));

        auto const history = model.History();
        ASSERT_GE(history.networkReceiveBytesPerSecond.size(), 2u);
        EXPECT_DOUBLE_EQ(history.networkReceiveBytesPerSecond.back(), 2'000'000.0);
        EXPECT_DOUBLE_EQ(history.networkSendBytesPerSecond.back(), 500'000.0);
    }

    TEST(NetworkRateTest, UtilizationIsAProportionOfTheSlowerDirection)
    {
        NetworkActivity activity;
        activity.receiveLinkSpeedBps = 1'000'000'000;   // 1 Gbps
        activity.transmitLinkSpeedBps = 100'000'000;    // 100 Mbps

        // 12.5 MB/s is 100 Mbps in bits, which is all of the slower direction.
        activity.sentBytesPerSecond = 12'500'000.0;
        EXPECT_DOUBLE_EQ(activity.UtilizationPercent(), 100.0);

        activity.sentBytesPerSecond = 6'250'000.0;
        EXPECT_DOUBLE_EQ(activity.UtilizationPercent(), 50.0);
    }

    TEST(NetworkRateTest, UtilizationIsZeroWithoutALinkSpeed)
    {
        // A proportion needs a whole to be a proportion of. A missing link speed yields zero rather
        // than a division by zero or a made-up percentage.
        NetworkActivity activity;
        activity.receivedBytesPerSecond = 1'000'000.0;
        EXPECT_DOUBLE_EQ(activity.UtilizationPercent(), 0.0);
    }

    TEST(GpuHistoryTest, GpuUtilisationIsRecordedAsGiven)
    {
        // The GPU counters are already rates, so nothing is differenced: the first sample is as
        // meaningful as any later one.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        platform::SystemGpuInfo gpu;
        gpu.utilizationPercent = 42.5;
        gpu.available = true;

        model.SetHardwareCounters({}, {}, gpu, T0);

        auto const history = model.History();
        ASSERT_FALSE(history.gpuUtilization.empty());
        EXPECT_DOUBLE_EQ(history.gpuUtilization.back(), 42.5);

        SystemView const& latest = model.Latest();
        EXPECT_TRUE(latest.gpu.available);
        EXPECT_DOUBLE_EQ(latest.gpu.utilizationPercent, 42.5);
    }

    TEST(GpuHistoryTest, AnUnavailableReadingStillAdvancesTheSeries)
    {
        // Every series must keep the same length, or the charts plot against different axes.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        platform::SystemGpuInfo unavailable;
        unavailable.available = false;

        model.Update(platform::SystemCpuTimes{}, platform::SystemMemoryInfo{}, T0);
        model.SetHardwareCounters({}, {}, unavailable, T0);

        auto const history = model.History();
        EXPECT_EQ(history.gpuUtilization.size(), history.cpuTotal.size());
        EXPECT_DOUBLE_EQ(history.gpuUtilization.back(), 0.0);
    }

    TEST(HardwareHistoryTest, EverySeriesHasTheSameLength)
    {
        // The charts share one time axis, which only holds if every ring advances on every sample.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        for (uint64_t i = 0; i < 5; ++i)
        {
            uint64_t const now = _afterMs(i * 1000);
            model.Update(platform::SystemCpuTimes{}, platform::SystemMemoryInfo{}, now);

            platform::SystemGpuInfo gpu;
            gpu.utilizationPercent = static_cast<double>(i);
            gpu.available = true;

            model.SetHardwareCounters({_disk("0 C:", i * 1000, i * 500, i, i)}, {_network("Ethernet", i * 100, i * 50)}, gpu, now);
        }

        auto const history = model.History();
        size_t const expected = history.cpuTotal.size();

        EXPECT_EQ(history.memoryUsed.size(), expected);
        EXPECT_EQ(history.diskReadBytesPerSecond.size(), expected);
        EXPECT_EQ(history.diskWriteBytesPerSecond.size(), expected);
        EXPECT_EQ(history.networkReceiveBytesPerSecond.size(), expected);
        EXPECT_EQ(history.networkSendBytesPerSecond.size(), expected);
        EXPECT_EQ(history.gpuUtilization.size(), expected);
    }
}
