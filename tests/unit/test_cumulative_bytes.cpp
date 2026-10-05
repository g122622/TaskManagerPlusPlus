// Tests for the session-cumulative byte totals on the performance sidebar's rows.
//
// These totals answer "how much has this application seen this device or adapter do", so they begin at
// zero when the application does rather than when the machine did. Three properties matter and are pinned
// here for each of the two:
//
//   * the total is the sum of the interval deltas, so it equals the bytes that actually moved;
//   * a device or adapter whose counters went backwards -- a reset or a replacement -- does not add a
//     spurious jump, because the interval is excluded rather than differenced;
//   * the totals are keyed by name, so one device's or adapter's traffic never lands on another's row.
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

        /// Builds a device reading with explicit byte counts, which is what the totals integrate.
        [[nodiscard]] platform::SystemDiskCounters _disk(std::string instanceName,
                                                         uint64_t readBytes,
                                                         uint64_t writeBytes)
        {
            platform::SystemDiskCounters disk;
            disk.instanceName = std::move(instanceName);
            disk.readBytes = readBytes;
            disk.writeBytes = writeBytes;

            // Only the byte counters are exercised here, so the others are held still. A zero idle counter
            // reads as a fully busy device, which is irrelevant to the total.
            disk.idleTimeMs = 0;
            disk.available = true;
            return disk;
        }

        /// Builds an adapter reading with explicit byte counts.
        [[nodiscard]] platform::SystemNetworkCounters _adapter(std::string adapterName,
                                                               uint64_t receivedBytes,
                                                               uint64_t sentBytes)
        {
            platform::SystemNetworkCounters iface;
            iface.adapterName = std::move(adapterName);
            iface.receivedBytes = receivedBytes;
            iface.sentBytes = sentBytes;
            iface.available = true;
            return iface;
        }

        /// One device's row, looked up by name so the test does not depend on list order.
        [[nodiscard]] DiskActivity const* _findDisk(SystemView const& view, std::string const& instanceName)
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

        /// One adapter's row, looked up by name.
        [[nodiscard]] NetworkActivity const* _findAdapter(SystemView const& view, std::string const& adapterName)
        {
            for (NetworkActivity const& iface : view.networks)
            {
                if (iface.adapterName == adapterName)
                {
                    return &iface;
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

        DiskActivity const* disk = _findDisk(model.Latest(), "0 C:");
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

        DiskActivity const* disk = _findDisk(model.Latest(), "0 C:");
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

        DiskActivity const* disk = _findDisk(model.Latest(), "0 C:");
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

        DiskActivity const* before = _findDisk(model.Latest(), "0 C:");
        ASSERT_NE(before, nullptr);
        double const totalBeforeReset = before->readBytesTotal;
        EXPECT_DOUBLE_EQ(totalBeforeReset, 1'000'000.0);

        // The counters restart from a low value.
        model.SetHardwareCounters({_disk("0 C:", 100, 0)}, {}, {}, _afterMs(2000));

        DiskActivity const* after = _findDisk(model.Latest(), "0 C:");
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

        DiskActivity const* first = _findDisk(model.Latest(), "0 C:");
        DiskActivity const* second = _findDisk(model.Latest(), "1 D:");
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);

        EXPECT_DOUBLE_EQ(first->readBytesTotal, 1'500'000'000.0);
        EXPECT_DOUBLE_EQ(second->readBytesTotal, 0.0) << "the idle device must not inherit the busy one's total";
    }

    TEST(NetworkCumulativeBytesTest, AnAdapterSeenForTheFirstTimeHasNoTotals)
    {
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 5'000'000, 9'000'000)}, {}, T0);

        NetworkActivity const* iface = _findAdapter(model.Latest(), "Wi-Fi");
        ASSERT_NE(iface, nullptr);
        EXPECT_DOUBLE_EQ(iface->receivedBytesTotal, 0.0);
        EXPECT_DOUBLE_EQ(iface->sentBytesTotal, 0.0);
    }

    TEST(NetworkCumulativeBytesTest, TheTotalsAreTheBytesThatMovedAndTheyAccumulate)
    {
        // Two intervals, a megabyte received in the first and half of one sent in the second, so the
        // totals are the sum rather than either delta.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 0, 0)}, {}, T0);
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 1'000'000, 0)}, {}, _afterMs(1000));
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 1'000'000, 500'000)}, {}, _afterMs(2000));

        NetworkActivity const* iface = _findAdapter(model.Latest(), "Wi-Fi");
        ASSERT_NE(iface, nullptr);
        EXPECT_DOUBLE_EQ(iface->receivedBytesTotal, 1'000'000.0);
        EXPECT_DOUBLE_EQ(iface->sentBytesTotal, 500'000.0);
    }

    TEST(NetworkCumulativeBytesTest, AnAdapterResetDoesNotAddASpuriousJump)
    {
        // The adapter's counters restart -- a driver reload, say. The total has to hold rather than
        // falling back to zero and climbing again.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 0, 0)}, {}, T0);
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 2'000'000, 0)}, {}, _afterMs(1000));

        NetworkActivity const* before = _findAdapter(model.Latest(), "Wi-Fi");
        ASSERT_NE(before, nullptr);
        double const totalBeforeReset = before->receivedBytesTotal;
        EXPECT_DOUBLE_EQ(totalBeforeReset, 2'000'000.0);

        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 10, 0)}, {}, _afterMs(2000));

        NetworkActivity const* after = _findAdapter(model.Latest(), "Wi-Fi");
        ASSERT_NE(after, nullptr);
        EXPECT_DOUBLE_EQ(after->receivedBytesTotal, totalBeforeReset) << "a reset must not change the total";
    }

    TEST(NetworkCumulativeBytesTest, TotalsAreKeptPerAdapterNotPerPosition)
    {
        // Two adapters, one busy and one idle, swapping places in the enumeration.
        SystemModel model(8, INTERVAL_MS, HISTORY_SECONDS);

        model.SetHardwareCounters({}, {_adapter("Ethernet", 0, 0), _adapter("Wi-Fi", 0, 0)}, {}, T0);
        model.SetHardwareCounters({}, {_adapter("Ethernet", 4'000'000, 0), _adapter("Wi-Fi", 0, 0)}, {},
                                  _afterMs(1000));
        model.SetHardwareCounters({}, {_adapter("Wi-Fi", 0, 0), _adapter("Ethernet", 4'500'000, 0)}, {},
                                  _afterMs(2000));

        NetworkActivity const* busy = _findAdapter(model.Latest(), "Ethernet");
        NetworkActivity const* idle = _findAdapter(model.Latest(), "Wi-Fi");
        ASSERT_NE(busy, nullptr);
        ASSERT_NE(idle, nullptr);

        EXPECT_DOUBLE_EQ(busy->receivedBytesTotal, 4'500'000.0);
        EXPECT_DOUBLE_EQ(idle->receivedBytesTotal, 0.0) << "the idle adapter must not inherit the busy one's total";
    }
}
