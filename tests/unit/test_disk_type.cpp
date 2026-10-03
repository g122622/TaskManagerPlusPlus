// Tests for the disk device-type classification.
//
// The type is what tells a user whether a slow volume is a spinning disk or a failing solid-state
// one, and it cannot be inferred from the model string: "Samsung SSD 990" contains "SSD" but
// "ST2000VX003" says nothing at all. The operating system answers the question directly, through
// the seek-penalty descriptor, and the bus type says which kind of solid-state device it is.
//
// The values here are the ones this machine actually reported, so the test records behaviour that
// was observed rather than behaviour that was assumed.
#include <gtest/gtest.h>

#include "Domain/SystemModel.h"

namespace tmpp::domain::test
{
    namespace
    {
        /// The bus types the storage descriptor reports, as observed.
        constexpr uint32_t BUS_TYPE_SATA = 11;
        constexpr uint32_t BUS_TYPE_NVME = 17;

        [[nodiscard]] DiskActivity _device(uint32_t busType, bool incursSeekPenalty, bool trimEnabled)
        {
            DiskActivity disk;
            disk.busType = busType;
            disk.incursSeekPenalty = incursSeekPenalty;
            disk.trimEnabled = trimEnabled;
            return disk;
        }
    }

    TEST(DiskTypeTest, ADeviceThatPaysForSeekingIsASpinningDisk)
    {
        // Both mechanical disks on this machine report a seek penalty and no TRIM.
        DiskActivity const disk = _device(BUS_TYPE_SATA, /*incursSeekPenalty=*/true, /*trimEnabled=*/false);
        EXPECT_EQ(disk.TypeName(), "HDD");
    }

    TEST(DiskTypeTest, ASolidStateDeviceOnPcieIsNvme)
    {
        // The Samsung 990 and the WD Blue SN5000 both report bus type 17 with no seek penalty.
        DiskActivity const disk = _device(BUS_TYPE_NVME, /*incursSeekPenalty=*/false, /*trimEnabled=*/true);
        EXPECT_EQ(disk.TypeName(), "SSD (NVMe)");
    }

    TEST(DiskTypeTest, ASolidStateDeviceOnSataIsSata)
    {
        // A solid-state device that answers the seek query but sits on the SATA bus. The
        // distinction matters because the two have very different ceilings.
        DiskActivity const disk = _device(BUS_TYPE_SATA, /*incursSeekPenalty=*/false, /*trimEnabled=*/true);
        EXPECT_EQ(disk.TypeName(), "SSD (SATA)");
    }

    TEST(DiskTypeTest, SeekPenaltyDecidesBeforeBusType)
    {
        // A device that pays for seeking is mechanical whatever bus it is on, which is why the
        // seek answer is consulted first. A SATA spinning disk must not be called an SSD just
        // because SATA also carries solid-state devices.
        DiskActivity const disk = _device(BUS_TYPE_SATA, /*incursSeekPenalty=*/true, /*trimEnabled=*/false);
        EXPECT_EQ(disk.TypeName(), "HDD");
    }

    TEST(DiskTypeTest, AnUnclassifiedDeviceIsAssumedMechanical)
    {
        // The default is the conservative one: the seek-penalty descriptor defaults to true because
        // a device that does not answer must not be presented as faster than it is.
        DiskActivity const disk;
        EXPECT_TRUE(disk.incursSeekPenalty) << "an unanswered query must not be read as solid-state";
        EXPECT_EQ(disk.TypeName(), "HDD");
    }
}
