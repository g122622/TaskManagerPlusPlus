// Tests for reading a published view between the two calls that build it.
//
// One sample is published in two steps: Update derives the CPU and memory figures and replaces the
// whole view, and SetHardwareCounters then fills in the device lists. A reader that arrives between
// them sees a view whose device lists are empty, and because SetHardwareCounters does not advance the
// version, the reader's cached version stays current and it skips every later refresh.
//
// That is what "sometimes the data is there and sometimes it is not" looks like from outside, and it
// is why the fields are now carried through by Update rather than left empty for the second call to
// fill.
#include <gtest/gtest.h>

#include <windows.h>

#include <vector>

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

        [[nodiscard]] platform::SystemDiskCounters _disk(std::string name)
        {
            platform::SystemDiskCounters disk;
            disk.instanceName = std::move(name);
            disk.readBytes = 1000;
            disk.idleTimeMs = 500;
            disk.capacityBytes = 500ull * 1024 * 1024 * 1024;
            disk.available = true;
            return disk;
        }
    }

    TEST(PublishedViewTest, TheDeviceListsSurviveAnUpdateThatDoesNotRefreshThem)
    {
        // The exact sequence a reader can observe. Update publishes a view on its own, and the device
        // lists have to still be there: the CPU and memory figures arrive on a different probe from the
        // hardware counters, and a reader cannot be asked to know that.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:"), _disk("1 D:")}, {}, {}, T0);
        ASSERT_EQ(model.Latest().disks.size(), 2u);

        // A later sample's CPU and memory half, published before its hardware half.
        model.Update(platform::SystemCpuTimes{}, platform::SystemMemoryInfo{}, _afterMs(1000));

        SystemView const& between = model.Latest();
        EXPECT_EQ(between.disks.size(), 2u)
            << "the device list disappeared between the two halves of a sample, which is what a reader "
               "arriving mid-sample sees";
        // Only disks were supplied, so an empty network list is correct here.
        EXPECT_TRUE(between.networks.empty());
    }

    TEST(PublishedViewTest, TheDeviceListsAreNotEmptiedByAnyUpdate)
    {
        // Repeated, because the defect was intermittent: it depended on where the reader's poll fell
        // relative to the two halves of a sample.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:")}, {}, {}, T0);

        for (uint32_t i = 1; i <= 50; ++i)
        {
            model.Update(platform::SystemCpuTimes{}, platform::SystemMemoryInfo{}, _afterMs(i * 1000));

            ASSERT_EQ(model.Latest().disks.size(), 1u)
                << "the device list was empty after Update on iteration " << i;
        }
    }

    TEST(PublishedViewTest, TheGpuReadingSurvivesAnUpdate)
    {
        // The GPU row showed "not collected yet" for the same reason and is carried through the same
        // way.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        platform::SystemGpuInfo gpu;
        gpu.utilizationPercent = 37.0;
        gpu.dedicatedTotalBytes = 8ull * 1024 * 1024 * 1024;
        gpu.adapterName = "Test Adapter";
        gpu.available = true;

        model.SetHardwareCounters({_disk("0 C:")}, {}, gpu, T0);
        ASSERT_TRUE(model.Latest().gpu.available);

        model.Update(platform::SystemCpuTimes{}, platform::SystemMemoryInfo{}, _afterMs(1000));

        EXPECT_TRUE(model.Latest().gpu.available)
            << "the GPU reading disappeared between the two halves of a sample";
        EXPECT_DOUBLE_EQ(model.Latest().gpu.utilizationPercent, 37.0);
        EXPECT_EQ(model.Latest().gpu.adapterName, "Test Adapter");
    }

    TEST(PublishedViewTest, ASampleThatReportsNoDevicesLeavesThePreviousOnesStanding)
    {
        // A read that reports nothing is a failure to observe, and the coordinator keeps the previous
        // reading rather than publishing emptiness. The model is the other half of that: it must not
        // erase what it already holds when it is told about nothing.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:"), _disk("1 D:")}, {}, {}, T0);
        ASSERT_EQ(model.Latest().disks.size(), 2u);

        // What the coordinator now sends instead of the empty list: the reading it already had.
        model.SetHardwareCounters({_disk("0 C:"), _disk("1 D:")}, {}, {}, _afterMs(1000));

        EXPECT_EQ(model.Latest().disks.size(), 2u);
    }
}
