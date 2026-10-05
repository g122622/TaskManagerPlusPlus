// Tests for the session-cumulative disk byte totals.
//
// The totals answer "how much has this application seen this device do", so they begin at zero when the
// application does rather than when the machine did. Three properties matter and are pinned here:
//
//   * the total is the sum of the interval deltas, so it equals the bytes that actually moved;
//   * a device whose counters went backwards -- a reset or a replacement -- does not add a spurious
//     jump, because the interval is excluded rather than differenced;
//   * the totals are keyed per device, so one device's traffic never lands on another's row.
#include <gtest/gtest.h>

#include <windows.h>

#include <string>

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

        /// Builds a device reading with an explicit byte count, which is what these totals integrate.
        [[nodiscard]] platform::SystemDiskCounters _disk(std::string instanceName,
                                                         uint64_t readBytes,
                                                         uint64_t writeBytes)
        {
            platform::SystemDiskCounters disk;
            disk.instanceName = std::move(instanceName);
            disk.readBytes = readBytes;
            disk.writeBytes = writeBytes;

            // The idle counter has to hold still or it does not matter here; only the byte counters are
            // exercised. Kept at zero so the device reads as fully busy, which is irrelevant to the total.
            disk.idleTimeMs = 0;
            disk.available = true;
            return disk;
        }

        /// The total for one device, looked up by name so the test does not depend on list order.
        [[nodiscard]] DiskActivity const* _find(SystemView const& view, std::string const& instanceName)
        {
            for (DiskActivity const& disk : view.disks)
            {
                if (disk.instanceName == instanceName)
                {
                    return &disk;
                }
            }
            return nullptr;
        }
    }

    TEST(DiskCumulativeBytesTest, ADeviceSeenForTheFirstTimeHasNoTotals)
    {
        // There is nothing to integrate over on the first sample, and reporting the device's own
        // since-boot counters would claim the application had seen traffic it never measured.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({_disk("0 C:", 5'000'000, 9'000'000)}, {}, {}, T0);

        SystemView const& latest = model.Latest();
        DiskActivity const* disk = _find(latest, "0 C:");
        ASSERT_NE(disk, nullptr);
        EXPECT_DOUBLE_EQ(disk->readBytesTotal, 0.0);
        EXPECT_DOUBLE_EQ(disk->writeBytesTotal, 0.0);
    }

    TEST(DiskCumulativeBytesTest, TheTotalIsTheBytesThatMovedInTheInterval)
    {
        // One megabyte read and half a megabyte written between the two samples. Integrating the rate
        // over the interval has to give back exactly that.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({_disk("0 C:", 1'000'000, 2'000'000)}, {}, {}, T0);
        model.SetHardwareCounters({_disk("0 C:", 2'000'000, 2'500'000)}, {}, {}, _afterMs(1000));

        DiskActivity const* disk = _find(model.Latest(), "0 C:");
        ASSERT_NE(disk, nullptr);
        EXPECT_DOUBLE_EQ(disk->readBytesTotal, 1'000'000.0);
        EXPECT_DOUBLE_EQ(disk->writeBytesTotal, 500'000.0);
    }

    TEST(DiskCumulativeBytesTest, TotalsAccumulateAcrossSamples)
    {
        // Three intervals of a megabyte each, so the running total is the sum rather than the last delta.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({_disk("0 C:", 0, 0)}, {}, {}, T0);
        model.SetHardwareCounters({_disk("0 C:", 1'000'000, 0)}, {}, {}, _afterMs(1000));
        model.SetHardwareCounters({_disk("0 C:", 2'000'000, 0)}, {}, {}, _afterMs(2000));
        model.SetHardwareCounters({_disk("0 C:", 3'000'000, 0)}, {}, {}, _afterMs(3000));

        DiskActivity const* disk = _find(model.Latest(), "0 C:");
        ASSERT_NE(disk, nullptr);
        EXPECT_DOUBLE_EQ(disk->readBytesTotal, 3'000'000.0);
    }

    TEST(DiskCumulativeBytesTest, AResetCounterDoesNotAddASpuriousJump)
    {
        // The device was reset between samples, so its counters restarted. Differencing would give a
        // negative rate, and discarding the negative while keeping the magnitude would invent traffic.
        // The total has to hold at what it had.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({_disk("0 C:", 1'000'000, 0)}, {}, {}, T0);
        model.SetHardwareCounters({_disk("0 C:", 2'000'000, 0)}, {}, {}, _afterMs(1000));

        DiskActivity const* before = _find(model.Latest(), "0 C:");
        ASSERT_NE(before, nullptr);
        double const totalBeforeReset = before->readBytesTotal;
        EXPECT_DOUBLE_EQ(totalBeforeReset, 1'000'000.0);

        // The counters restart from a low value.
        model.SetHardwareCounters({_disk("0 C:", 100, 0)}, {}, {}, _afterMs(2000));

        DiskActivity const* after = _find(model.Latest(), "0 C:");
        ASSERT_NE(after, nullptr);
        EXPECT_DOUBLE_EQ(after->readBytesTotal, totalBeforeReset) << "a reset must not change the total";
    }

    TEST(DiskCumulativeBytesTest, TotalsAreKeptPerDeviceNotPerPosition)
    {
        // Two devices swapping places in the enumeration must not swap their totals. The rate path already
        // matches by instance name; this is the same requirement for the total.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({_disk("0 C:", 0, 0), _disk("1 D:", 0, 0)}, {}, {}, T0);

        // The first device moves a gigabyte; the second stays still.
        model.SetHardwareCounters({_disk("0 C:", 1'000'000'000, 0), _disk("1 D:", 0, 0)}, {}, {},
                                  _afterMs(1000));

        // The list comes back in the other order.
        model.SetHardwareCounters({_disk("1 D:", 0, 0), _disk("0 C:", 1'500'000'000, 0)}, {}, {},
                                  _afterMs(2000));

        DiskActivity const* first = _find(model.Latest(), "0 C:");
        DiskActivity const* second = _find(model.Latest(), "1 D:");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);

        EXPECT_DOUBLE_EQ(first->readBytesTotal, 1'500'000'000.0);
        EXPECT_DOUBLE_EQ(second->readBytesTotal, 0.0) << "the idle device must not inherit the busy one's total";
    }
}
