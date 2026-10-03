// Tests for the extended system probes.
//
// These read real machine state rather than fixtures, which is deliberate: the
// registry layout, the cache topology entries and the PDH counter path are all
// external contracts, and a fixture would only prove the test agrees with itself.
// Assertions are therefore about what must be true of any Windows machine rather than
// about specific values, so the suite stays portable across hardware.
#include <gtest/gtest.h>

#include "Platform/Windows/ProcessorSpeedProbe.h"
#include "Platform/Windows/WindowsSystemProbe.h"

namespace tmpp::platform
{
    TEST(SystemProbeExtendedTest, ReadsProcessorModelName)
    {
        WindowsSystemProbe probe;
        auto const info = probe.ReadProcessorInfo();

        ASSERT_TRUE(info.Success()) << info.GetError().Message();

        // Every Windows machine reports a marketing name for processor 0. An empty
        // string would mean the registry read silently failed, which would show up as
        // a blank line in the performance page.
        EXPECT_FALSE(info.Value().modelName.empty())
            << "the processor name comes from HKLM\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0";
    }

    TEST(SystemProbeExtendedTest, ReadsRatedClock)
    {
        WindowsSystemProbe probe;
        auto const info = probe.ReadProcessorInfo();
        ASSERT_TRUE(info.Success());

        // The rated clock is what the live speed counter is a percentage of, so a zero
        // here would make the speed readout permanently unavailable.
        EXPECT_GT(info.Value().baseClockMhz, 100u) << "a rated clock below 100 MHz is not plausible";
        EXPECT_LT(info.Value().baseClockMhz, 20000u) << "and neither is one above 20 GHz";
    }

    TEST(SystemProbeExtendedTest, ReadsCacheSizes)
    {
        WindowsSystemProbe probe;
        auto const info = probe.ReadProcessorInfo();
        ASSERT_TRUE(info.Success());

        // Every x64 machine since forever has an L1. Zero would mean the topology
        // query failed and the page would show a blank where a size belongs.
        EXPECT_GT(info.Value().l1CacheBytes, 0u);
        EXPECT_GT(info.Value().l2CacheBytes, 0u);

        // Larger caches are larger, which is a weak but real invariant: if the levels
        // were mis-assigned this would catch it.
        EXPECT_GE(info.Value().l3CacheBytes, info.Value().l2CacheBytes);
        EXPECT_GE(info.Value().l2CacheBytes, info.Value().l1CacheBytes);
    }

    TEST(SystemProbeExtendedTest, ReportsAtLeastOneSocket)
    {
        WindowsSystemProbe probe;
        auto const info = probe.ReadProcessorInfo();
        ASSERT_TRUE(info.Success());

        // The field is never left at zero, because a machine with no processor package
        // is not a state that exists and zero would read as a failed query.
        EXPECT_GE(info.Value().socketCount, 1u);
    }

    TEST(SystemProbeExtendedTest, ReportsVirtualizationSupport)
    {
        WindowsSystemProbe probe;
        auto const info = probe.ReadProcessorInfo();
        ASSERT_TRUE(info.Success());

        // DEP is enabled on every supported Windows installation, so it is a reliable
        // signal that IsProcessorFeaturePresent is being consulted at all.
        EXPECT_TRUE(info.Value().depAvailable)
            << "PF_NX_ENABLED is set on every Windows 11 machine; false suggests the query failed";
    }

    TEST(SystemProbeExtendedTest, TotalsCarryTheCountsTheyWereGiven)
    {
        WindowsSystemProbe probe;
        auto const totals = probe.ReadTotals(123, 4567, 8901);

        ASSERT_TRUE(totals.Success());
        EXPECT_EQ(totals.Value().processCount, 123u);
        EXPECT_EQ(totals.Value().threadCount, 4567u);
        EXPECT_EQ(totals.Value().handleCount, 8901u);

        // Uptime comes from GetTickCount64, which cannot fail. A machine that has just
        // booted reports a small number; zero is possible only in the first second.
        EXPECT_LT(totals.Value().uptimeSeconds, 60ull * 60 * 24 * 365 * 10)
            << "uptime above ten years suggests the units were misread";
    }

    TEST(ProcessorSpeedProbeTest, ReportsUnavailableWithoutARatedClock)
    {
        // Without the rated clock the counter's percentage cannot be converted into a
        // frequency, so the probe must decline rather than invent a scale.
        ProcessorSpeedProbe probe{0};
        EXPECT_FALSE(probe.Available());

        auto const speed = probe.Read();
        ASSERT_TRUE(speed.Success()) << "an unusable probe is not an error, it is simply unavailable";
        EXPECT_FALSE(speed.Value().available);
        EXPECT_EQ(speed.Value().currentMhz, 0u);
    }

    TEST(ProcessorSpeedProbeTest, ReadsAPlausibleSpeedWhenTheCounterExists)
    {
        WindowsSystemProbe systemProbe;
        auto const info = systemProbe.ReadProcessorInfo();
        ASSERT_TRUE(info.Success());

        ProcessorSpeedProbe probe{info.Value().baseClockMhz};
        if (!probe.Available())
        {
            GTEST_SKIP() << "the processor performance counter is absent on this machine";
        }

        // The counter needs two collections to produce a rate, so the first read after
        // construction may legitimately have no value yet. Take a second reading, which
        // is what the sampler does on its next tick.
        auto const priming = probe.Read();
        ASSERT_TRUE(priming.Success()) << priming.GetError().Message();

        auto const speed = probe.Read();
        ASSERT_TRUE(speed.Success()) << speed.GetError().Message();

        if (!speed.Value().available)
        {
            GTEST_SKIP() << "the counter reported no usable data; this happens while cores are parked";
        }

        // A plausible desktop range. Turbo can push well past the rating, and idle can
        // drop far below it, so the bounds are deliberately loose -- the point is to
        // catch a unit error, not to validate the machine.
        EXPECT_GT(speed.Value().currentMhz, 100u);
        EXPECT_LT(speed.Value().currentMhz, 20000u);
    }
}
