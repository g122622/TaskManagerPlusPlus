// Tests for the interval-mean response time.
//
// Response time is a mean, and the device reports its own lifetime means. Differencing two of those
// does not give the mean over the interval between them -- it gives something that happens to have
// the right units. The figure has to be computed from the service-time and operation deltas, which
// is what this pins down.
#include <gtest/gtest.h>

#include <windows.h>

#include "Domain/SystemModel.h"

namespace tmpp::domain::test
{
    namespace
    {
        constexpr uint32_t INTERVAL_MS = 1000;
        constexpr uint32_t HISTORY_SECONDS = 60;

        [[nodiscard]] uint64_t _ticksPerSecond() noexcept
        {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency(&frequency);
            return static_cast<uint64_t>(frequency.QuadPart);
        }

        uint64_t const T0 = 1000000;

        [[nodiscard]] uint64_t _afterMs(uint64_t millis) noexcept
        {
            return T0 + ((_ticksPerSecond() * millis) / 1000);
        }

        struct DiskReading
        {
            uint64_t readCount{0};
            uint64_t writeCount{0};
            uint64_t readTimeMs{0};
            uint64_t writeTimeMs{0};
            uint64_t idleTimeMs{0};
        };

        [[nodiscard]] platform::SystemDiskCounters _disk(DiskReading const& reading)
        {
            platform::SystemDiskCounters disk;
            disk.instanceName = "0 C:";
            disk.readCount = reading.readCount;
            disk.writeCount = reading.writeCount;
            disk.readTimeMs = reading.readTimeMs;
            disk.writeTimeMs = reading.writeTimeMs;
            disk.idleTimeMs = reading.idleTimeMs;
            disk.available = true;
            return disk;
        }
    }

    TEST(DiskResponseTimeTest, IsTheServiceTimeOverTheOperationsInTheInterval)
    {
        // Over one second: four operations completed and eight milliseconds of service time spent.
        // The mean is two milliseconds, whatever the device's lifetime average happens to be.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk(DiskReading{100, 200, 5000, 5000, 0})}, {}, {}, T0);

        // Two more reads and two more writes, adding eight milliseconds of service time.
        model.SetHardwareCounters({_disk(DiskReading{102, 202, 5004, 5004, 0})}, {}, {}, _afterMs(1000));

        SystemView const& latest = model.Latest();
        ASSERT_EQ(latest.disks.size(), 1u);
        EXPECT_DOUBLE_EQ(latest.disks[0].averageResponseMs, 2.0);
    }

    TEST(DiskResponseTimeTest, LifetimeAveragesWouldGiveTheWrongAnswer)
    {
        // This is the mistake the derivation avoids. The device's lifetime mean before the interval
        // was 5000/300 = 16.67 ms and after it 5008/304 = 16.47 ms; differencing those would give a
        // negative figure, and averaging them would give 16.57 ms. The interval's own mean is 2 ms.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk(DiskReading{100, 200, 5000, 5000, 0})}, {}, {}, T0);
        model.SetHardwareCounters({_disk(DiskReading{102, 202, 5004, 5004, 0})}, {}, {}, _afterMs(1000));

        SystemView const& latest = model.Latest();
        ASSERT_EQ(latest.disks.size(), 1u);

        double const lifetimeMeanAfter = 5008.0 / 304.0;
        EXPECT_LT(latest.disks[0].averageResponseMs, lifetimeMeanAfter / 2.0)
            << "the interval mean is far below the device's lifetime mean";
        EXPECT_GE(latest.disks[0].averageResponseMs, 0.0) << "a response time cannot be negative";
    }

    TEST(DiskResponseTimeTest, IsZeroWhenNoOperationCompleted)
    {
        // An idle interval has no mean. Reporting zero is correct here rather than a placeholder:
        // nothing was requested, so nothing took any time.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk(DiskReading{100, 200, 5000, 5000, 0})}, {}, {}, T0);
        model.SetHardwareCounters({_disk(DiskReading{100, 200, 5000, 5000, 1000})}, {}, {}, _afterMs(1000));

        SystemView const& latest = model.Latest();
        ASSERT_EQ(latest.disks.size(), 1u);
        EXPECT_DOUBLE_EQ(latest.disks[0].averageResponseMs, 0.0);
        EXPECT_DOUBLE_EQ(latest.disks[0].activePercent, 0.0);
    }

    TEST(DiskResponseTimeTest, IsComputedPerDevice)
    {
        // Each device's response time is its own; averaging across devices would hide a slow one
        // behind a fast one, which is exactly what a user is looking for.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        platform::SystemDiskCounters fast;
        fast.instanceName = "0 C:";
        fast.readCount = 100;
        fast.readTimeMs = 0;
        fast.idleTimeMs = 0;
        fast.available = true;

        platform::SystemDiskCounters slow;
        slow.instanceName = "1 D:";
        slow.readCount = 10;
        slow.readTimeMs = 1000;
        slow.idleTimeMs = 0;
        slow.available = true;

        model.SetHardwareCounters({fast, slow}, {}, {}, T0);

        // The fast device gains one operation in one millisecond; the slow one gains one in a
        // hundred.
        fast.readCount = 101;
        fast.readTimeMs = 1;
        slow.readCount = 11;
        slow.readTimeMs = 1100;

        model.SetHardwareCounters({fast, slow}, {}, {}, _afterMs(1000));

        SystemView const& latest = model.Latest();
        ASSERT_EQ(latest.disks.size(), 2u);

        // Found by name, since the order is not guaranteed.
        double fastMs = -1.0;
        double slowMs = -1.0;
        for (DiskActivity const& disk : latest.disks)
        {
            if (disk.instanceName == "0 C:")
            {
                fastMs = disk.averageResponseMs;
            }
            else if (disk.instanceName == "1 D:")
            {
                slowMs = disk.averageResponseMs;
            }
        }

        EXPECT_DOUBLE_EQ(fastMs, 1.0);
        EXPECT_DOUBLE_EQ(slowMs, 100.0);
    }
}
