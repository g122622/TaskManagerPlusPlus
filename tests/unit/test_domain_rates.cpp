// Tests for the Domain layer's counter-to-rate arithmetic.
//
// Unlike the Platform tests, these are pure computation against synthetic
// counters. That is the point: the edge cases that matter here (counter
// rollback, PID reuse, zero intervals, a clock that goes backwards) cannot be
// provoked reliably on a live system, but they are trivial to state directly.
#include <gtest/gtest.h>

#include "Domain/RateMath.h"
#include "Domain/RingBuffer.h"
#include "Domain/SamplingConfig.h"

#include <limits>
#include <string>

namespace tmpp::domain
{
    // ------------------------------------------------------------------------
    // ComputeDelta
    // ------------------------------------------------------------------------

    TEST(RateMathTest, DeltaOfIncreasingCounters)
    {
        Delta const delta = ComputeDelta(1000, 1500);
        EXPECT_TRUE(delta.Usable());
        EXPECT_EQ(delta.value, 500u);
    }

    TEST(RateMathTest, DeltaOfZeroChangeIsZeroAndUsable)
    {
        // An idle process accumulates no CPU time; that is a real reading, not an
        // error, and must render as 0 rather than as "unavailable".
        Delta const delta = ComputeDelta(1000, 1000);
        EXPECT_TRUE(delta.Usable());
        EXPECT_EQ(delta.value, 0u);
    }

    TEST(RateMathTest, DeltaDetectsCounterRollback)
    {
        // A process that restarted, or a recycled PID, produces a lower reading.
        Delta const delta = ComputeDelta(5000, 100);
        EXPECT_FALSE(delta.Usable());
        EXPECT_EQ(delta.status, DeltaStatus::CounterReset);
        EXPECT_EQ(delta.value, 0u);
    }

    TEST(RateMathTest, DeltaHandlesFullWidthValues)
    {
        // 64-bit counters must not overflow when their difference is taken.
        uint64_t const max = std::numeric_limits<uint64_t>::max();
        Delta const delta = ComputeDelta(0, max);
        EXPECT_TRUE(delta.Usable());
        EXPECT_EQ(delta.value, max);
    }

    TEST(RateMathTest, DeltaFromZeroBaseline)
    {
        // A freshly created process legitimately starts near zero.
        Delta const delta = ComputeDelta(0, 12345);
        EXPECT_TRUE(delta.Usable());
        EXPECT_EQ(delta.value, 12345u);
    }

    // ------------------------------------------------------------------------
    // ComputePercentage
    // ------------------------------------------------------------------------

    TEST(RateMathTest, PercentageOfWhole)
    {
        EXPECT_DOUBLE_EQ(ComputePercentage(50, 100).value, 50.0);
        EXPECT_DOUBLE_EQ(ComputePercentage(1, 4).value, 25.0);
    }

    TEST(RateMathTest, PercentageWithZeroWholeIsZeroAndClamped)
    {
        // Dividing by zero must not produce NaN or infinity.
        Percentage const result = ComputePercentage(10, 0);
        EXPECT_DOUBLE_EQ(result.value, 0.0);
        EXPECT_TRUE(result.clamped);
    }

    TEST(RateMathTest, PercentageClampsAboveCeiling)
    {
        // Counter glitches must not draw a chart spike above 100%.
        Percentage const result = ComputePercentage(200, 100);
        EXPECT_DOUBLE_EQ(result.value, 100.0);
        EXPECT_TRUE(result.clamped);
    }

    TEST(RateMathTest, PercentageRespectsCustomCeiling)
    {
        // A process on an 8-core machine may legitimately reach 800%.
        Percentage const result = ComputePercentage(10000, 1000, 800.0);
        EXPECT_DOUBLE_EQ(result.value, 800.0);
        EXPECT_TRUE(result.clamped);
    }

    TEST(RateMathTest, PercentageOfZeroPart)
    {
        Percentage const result = ComputePercentage(0, 100);
        EXPECT_DOUBLE_EQ(result.value, 0.0);
        EXPECT_FALSE(result.clamped);
    }

    // ------------------------------------------------------------------------
    // ComputeRatePerSecond
    // ------------------------------------------------------------------------

    TEST(RateMathTest, RatePerSecondScalesByInterval)
    {
        // 1000 units in 1000 ms is 1000/s.
        EXPECT_DOUBLE_EQ(ComputeRatePerSecond(1000, 1000), 1000.0);
        // The same 1000 units in 500 ms is 2000/s.
        EXPECT_DOUBLE_EQ(ComputeRatePerSecond(1000, 500), 2000.0);
        // ...and in 2000 ms it is 500/s.
        EXPECT_DOUBLE_EQ(ComputeRatePerSecond(1000, 2000), 500.0);
    }

    TEST(RateMathTest, RateWithZeroIntervalIsZero)
    {
        // Two samples at the same instant have no meaningful rate; returning 0 is
        // safer than returning infinity.
        EXPECT_DOUBLE_EQ(ComputeRatePerSecond(5000, 0), 0.0);
    }

    TEST(RateMathTest, RateOfNoChangeIsZero)
    {
        EXPECT_DOUBLE_EQ(ComputeRatePerSecond(0, 1000), 0.0);
    }

    // ------------------------------------------------------------------------
    // ComputeProcessCpuPercent
    // ------------------------------------------------------------------------

    TEST(RateMathTest, ProcessCpuPercentSingleCore)
    {
        // One of one logical processor fully busy is 100%.
        Percentage const result = ComputeProcessCpuPercent(1000, 1000, 1);
        EXPECT_DOUBLE_EQ(result.value, 100.0);
    }

    TEST(RateMathTest, ProcessCpuPercentScalesWithProcessorCount)
    {
        // The process used half of the total CPU time on a 4-processor machine, so
        // it was using 2 processors' worth: 200%.
        Percentage const result = ComputeProcessCpuPercent(500, 1000, 4);
        EXPECT_DOUBLE_EQ(result.value, 200.0);
    }

    TEST(RateMathTest, ProcessCpuPercentCeilingIsCoreCountTimesHundred)
    {
        // A process cannot exceed every processor running flat out. A reading
        // beyond that indicates a counter glitch and must be clamped.
        Percentage const result = ComputeProcessCpuPercent(999999, 1000, 4);
        EXPECT_DOUBLE_EQ(result.value, 400.0);
        EXPECT_TRUE(result.clamped);
    }

    TEST(RateMathTest, ProcessCpuPercentWithZeroSystemDelta)
    {
        // A system tick that recorded no CPU time at all must not divide by zero.
        Percentage const result = ComputeProcessCpuPercent(100, 0, 8);
        EXPECT_DOUBLE_EQ(result.value, 0.0);
        EXPECT_TRUE(result.clamped);
    }

    TEST(RateMathTest, ProcessCpuPercentWithZeroProcessors)
    {
        Percentage const result = ComputeProcessCpuPercent(100, 1000, 0);
        EXPECT_DOUBLE_EQ(result.value, 0.0);
        EXPECT_TRUE(result.clamped);
    }

    // ------------------------------------------------------------------------
    // ComputeSystemCpuPercent
    // ------------------------------------------------------------------------

    TEST(RateMathTest, SystemCpuPercentBusyFraction)
    {
        // A quarter of the interval was busy.
        EXPECT_DOUBLE_EQ(ComputeSystemCpuPercent(250, 750).value, 25.0);
        EXPECT_DOUBLE_EQ(ComputeSystemCpuPercent(500, 500).value, 50.0);
    }

    TEST(RateMathTest, SystemCpuPercentFullyIdle)
    {
        EXPECT_DOUBLE_EQ(ComputeSystemCpuPercent(0, 1000).value, 0.0);
    }

    TEST(RateMathTest, SystemCpuPercentFullyBusy)
    {
        EXPECT_DOUBLE_EQ(ComputeSystemCpuPercent(1000, 0).value, 100.0);
    }

    TEST(RateMathTest, SystemCpuPercentWithNoElapsedTime)
    {
        // Both deltas zero means the clock did not advance between samples.
        Percentage const result = ComputeSystemCpuPercent(0, 0);
        EXPECT_DOUBLE_EQ(result.value, 0.0);
        EXPECT_TRUE(result.clamped);
    }

    // ------------------------------------------------------------------------
    // Tick conversions
    // ------------------------------------------------------------------------

    TEST(RateMathTest, MillisecondsToTicksRoundTrip)
    {
        EXPECT_EQ(MillisecondsToTicks(1), 10000u);
        EXPECT_EQ(MillisecondsToTicks(1000), 10000000u);
        EXPECT_EQ(TicksToMilliseconds(MillisecondsToTicks(250)), 250u);
    }

    // ------------------------------------------------------------------------
    // SamplingConfig
    // ------------------------------------------------------------------------

    TEST(SamplingConfigTest, ClampIntervalKeepsValidValues)
    {
        EXPECT_EQ(sampling::ClampInterval(1000), 1000u);
        EXPECT_EQ(sampling::ClampInterval(sampling::MIN_INTERVAL_MS), sampling::MIN_INTERVAL_MS);
        EXPECT_EQ(sampling::ClampInterval(sampling::MAX_INTERVAL_MS), sampling::MAX_INTERVAL_MS);
    }

    TEST(SamplingConfigTest, ClampIntervalRejectsOutOfRange)
    {
        // A persisted setting from an older version, or a hand-edited config file,
        // must never reach the sampler unclamped.
        EXPECT_EQ(sampling::ClampInterval(0), sampling::MIN_INTERVAL_MS);
        EXPECT_EQ(sampling::ClampInterval(1), sampling::MIN_INTERVAL_MS);
        EXPECT_EQ(sampling::ClampInterval(999999), sampling::MAX_INTERVAL_MS);
    }

    TEST(SamplingConfigTest, ClampHistorySeconds)
    {
        EXPECT_EQ(sampling::ClampHistorySeconds(60), 60u);
        EXPECT_EQ(sampling::ClampHistorySeconds(0), sampling::MIN_HISTORY_SECONDS);
        EXPECT_EQ(sampling::ClampHistorySeconds(99999), sampling::MAX_HISTORY_SECONDS);
    }

    TEST(SamplingConfigTest, HistoryCapacityMatchesWindowAndInterval)
    {
        // 60 s at 1 s per sample is 60 points.
        EXPECT_EQ(sampling::HistoryCapacity(1000, 60), 60u);
        // A 5-minute window at the fastest interval is 600 points.
        EXPECT_EQ(sampling::HistoryCapacity(500, 300), 600u);
    }

    TEST(SamplingConfigTest, HistoryCapacityIsBounded)
    {
        // The hard cap must hold even for the largest window at the fastest rate.
        size_t const capacity = sampling::HistoryCapacity(sampling::MIN_INTERVAL_MS, sampling::MAX_HISTORY_SECONDS);
        EXPECT_LE(capacity, sampling::MAX_HISTORY_POINTS);
        // ...and a two-point minimum must always hold, so a line can be drawn.
        EXPECT_GE(capacity, 2u);
    }

    // ------------------------------------------------------------------------
    // RingBuffer
    // ------------------------------------------------------------------------

    TEST(RingBufferTest, PushBelowCapacityKeepsChronologicalOrder)
    {
        RingBuffer<int> buffer(8);
        buffer.Push(1);
        buffer.Push(2);
        buffer.Push(3);

        EXPECT_EQ(buffer.Size(), 3u);
        EXPECT_FALSE(buffer.Full());
        EXPECT_EQ(buffer.At(0), 1);
        EXPECT_EQ(buffer.At(1), 2);
        EXPECT_EQ(buffer.At(2), 3);
        EXPECT_EQ(buffer.Oldest(), 1);
        EXPECT_EQ(buffer.Latest(), 3);
    }

    TEST(RingBufferTest, OverwriteDropsOldestAndKeepsOrder)
    {
        RingBuffer<int> buffer(3);
        for (int i = 1; i <= 5; ++i)
        {
            buffer.Push(i);
        }

        // Cap of 3 after pushing 1..5 retains 3, 4, 5 in order.
        EXPECT_EQ(buffer.Size(), 3u);
        EXPECT_EQ(buffer.Capacity(), 3u);
        EXPECT_TRUE(buffer.Full());
        EXPECT_EQ(buffer.Oldest(), 3);
        EXPECT_EQ(buffer.Latest(), 5);
        EXPECT_EQ(buffer.At(0), 3);
        EXPECT_EQ(buffer.At(1), 4);
        EXPECT_EQ(buffer.At(2), 5);
    }

    TEST(RingBufferTest, MemoryStaysConstantUnderSustainedPush)
    {
        // History must not grow with uptime. Pushing far more than the capacity
        // must leave the size pinned at capacity.
        RingBuffer<double> buffer(64);
        for (int i = 0; i < 100000; ++i)
        {
            buffer.Push(static_cast<double>(i));
        }

        EXPECT_EQ(buffer.Size(), 64u);
        EXPECT_EQ(buffer.Capacity(), 64u);
    }

    TEST(RingBufferTest, ToVectorMatchesIndexedAccess)
    {
        RingBuffer<int> buffer(4);
        for (int i = 1; i <= 6; ++i)
        {
            buffer.Push(i);
        }

        std::vector<int> const flattened = buffer.ToVector();
        ASSERT_EQ(flattened.size(), buffer.Size());
        for (size_t i = 0; i < flattened.size(); ++i)
        {
            EXPECT_EQ(flattened[i], buffer.At(i));
        }
    }

    TEST(RingBufferTest, ClearKeepsAllocation)
    {
        RingBuffer<int> buffer(8);
        buffer.Push(1);
        buffer.Push(2);
        buffer.Clear();

        EXPECT_TRUE(buffer.Empty());
        EXPECT_EQ(buffer.Size(), 0u);
        // Capacity survives, so a clear does not cause a reallocation.
        EXPECT_EQ(buffer.Capacity(), 8u);

        buffer.Push(42);
        EXPECT_EQ(buffer.Latest(), 42);
    }

    TEST(RingBufferTest, ZeroCapacityIsPromotedToOne)
    {
        // A capacity of zero would make Push write out of bounds; promoting it
        // keeps the buffer usable instead of undefined.
        RingBuffer<int> buffer(0);
        EXPECT_EQ(buffer.Capacity(), 1u);

        buffer.Push(7);
        EXPECT_EQ(buffer.Latest(), 7);
        EXPECT_TRUE(buffer.Full());
    }

    TEST(RingBufferTest, WorksWithNonTrivialTypes)
    {
        RingBuffer<std::string> buffer(2);
        buffer.Push("first");
        buffer.Push("second");
        buffer.Push("third");

        EXPECT_EQ(buffer.Size(), 2u);
        EXPECT_EQ(buffer.Oldest(), "second");
        EXPECT_EQ(buffer.Latest(), "third");
    }
}
