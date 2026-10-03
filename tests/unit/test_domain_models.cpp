// Tests for the Domain layer's models.
//
// These feed synthetic platform snapshots through the models so that PID reuse,
// process restarts and missing parents can be stated directly instead of being
// waited for on a live system.
#include <gtest/gtest.h>

#include "Domain/ProcessModel.h"
#include "Domain/SystemModel.h"

#include "Platform/Windows/WindowsProcessProbe.h"

// Test code is white-box and is not bound by the Domain layer's rule against
// including platform headers. The synthetic timestamps below must be in the same
// units the sampler produces (QueryPerformanceCounter ticks), so the real
// frequency is read here rather than assumed.
#include <windows.h>

namespace tmpp::domain
{
    namespace
    {
        /// Distinct creation times keep test identities independent unless a test
        /// deliberately reuses one.
        constexpr uint64_t CREATE_TIME_A = 1000000;
        constexpr uint64_t CREATE_TIME_B = 2000000;
        constexpr uint64_t CREATE_TIME_C = 3000000;

        /// Arbitrary but increasing QPC-like timestamps.
        ///
        /// Captured timestamps are QueryPerformanceCounter values, not
        /// milliseconds, so synthetic intervals must be expressed in the same
        /// units. These helpers derive the tick counts from the real frequency,
        /// otherwise a test that says "one second" would silently be 10.
        [[nodiscard]] uint64_t _ticksPerSecond() noexcept
        {
            LARGE_INTEGER frequency{};
            QueryPerformanceFrequency(&frequency);
            return static_cast<uint64_t>(frequency.QuadPart);
        }

        [[nodiscard]] uint64_t _secondsFrom(uint64_t base, uint64_t seconds) noexcept
        {
            return base + (_ticksPerSecond() * seconds);
        }

        uint64_t const T0 = 1000000;
        uint64_t const T1 = _secondsFrom(T0, 1);
        uint64_t const T2 = _secondsFrom(T0, 2);

        platform::ProcessInfo _makeProcess(uint32_t pid,
                                          uint64_t createTime,
                                          uint32_t parentPid = 0,
                                          std::string name = "proc.exe")
        {
            platform::ProcessInfo info;
            info.identity.pid = pid;
            info.identity.createTime = createTime;
            info.parentPid = parentPid;
            info.imageName = std::move(name);
            info.threadCount = 1;
            info.handleCount = 10;
            return info;
        }

        /// Total system CPU delta spanning one second on a single processor.
        SystemCpuDelta _systemDelta(uint64_t busy, uint64_t idle) { return SystemCpuDelta{busy, idle}; }
    }

    // ------------------------------------------------------------------------
    // ProcessModel
    // ------------------------------------------------------------------------

    TEST(ProcessModelTest, FirstSamplePublishesWithoutRates)
    {
        ProcessModel model(4);

        platform::ProcessSnapshot raw;
        raw.capturedAt = T0;
        raw.processes.push_back(_makeProcess(100, CREATE_TIME_A));

        ProcessSnapshotView const view = model.Update(raw, _systemDelta(0, 0));

        ASSERT_EQ(view.processes.size(), 1u);
        EXPECT_TRUE(view.processes[0].ratesUnavailable) << "no baseline exists yet, so rates must be flagged unavailable";
        EXPECT_DOUBLE_EQ(view.processes[0].cpuPercent, 0.0);
        EXPECT_EQ(view.version, 1u);
    }

    TEST(ProcessModelTest, SecondSampleDerivesCpuPercent)
    {
        ProcessModel model(1);

        platform::ProcessSnapshot first;
        first.capturedAt = T0;
        first.processes.push_back(_makeProcess(100, CREATE_TIME_A));
        model.Update(first, _systemDelta(0, 10000000));

        // Second sample: the process gained 250000 ticks while the system spent
        // 1000000 ticks busy and 9000000 idle over the interval.
        platform::ProcessSnapshot second;
        second.capturedAt = T1;
        auto process = _makeProcess(100, CREATE_TIME_A);
        process.cpu.userTime = 250000;
        second.processes.push_back(process);

        SystemCpuDelta const systemCpu = _systemDelta(1000000, 9000000);
        ProcessSnapshotView const view = model.Update(second, systemCpu);

        ASSERT_EQ(view.processes.size(), 1u);
        EXPECT_FALSE(view.processes[0].ratesUnavailable);
        // 250000 of 10000000 total system ticks, on 1 processor.
        EXPECT_NEAR(view.processes[0].cpuPercent, 2.5, 0.001);
    }

    TEST(ProcessModelTest, ReusedPidStartsFreshBaseline)
    {
        // This is the failure the identity key exists to prevent: a recycled PID
        // must not inherit the previous process's large counters, which would be
        // reported as an enormous bogus CPU spike.
        ProcessModel model(4);

        platform::ProcessSnapshot first;
        first.capturedAt = T0;
        auto oldProcess = _makeProcess(500, CREATE_TIME_A);
        oldProcess.cpu.userTime = 5000000000ull; // A long-lived process.
        first.processes.push_back(oldProcess);
        model.Update(first, _systemDelta(1000000, 1000000));

        // Same PID, different creation time: a new process.
        platform::ProcessSnapshot second;
        second.capturedAt = T1;
        auto newProcess = _makeProcess(500, CREATE_TIME_B);
        newProcess.cpu.userTime = 1000; // Just started.
        second.processes.push_back(newProcess);

        ProcessSnapshotView const view = model.Update(second, _systemDelta(1000000, 1000000));

        ASSERT_EQ(view.processes.size(), 1u);
        EXPECT_TRUE(view.processes[0].ratesUnavailable)
            << "a recycled PID must not difference against the previous process's counters";
        EXPECT_DOUBLE_EQ(view.processes[0].cpuPercent, 0.0);
    }

    TEST(ProcessModelTest, CounterRollbackMarksRatesUnavailable)
    {
        ProcessModel model(1);

        platform::ProcessSnapshot first;
        first.capturedAt = T0;
        auto process = _makeProcess(100, CREATE_TIME_A);
        process.cpu.userTime = 1000000;
        first.processes.push_back(process);
        model.Update(first, _systemDelta(1000000, 1000000));

        // The counter went backwards: the process restarted under the same
        // identity, which should not happen but must not produce garbage.
        platform::ProcessSnapshot second;
        second.capturedAt = T1;
        auto restarted = _makeProcess(100, CREATE_TIME_A);
        restarted.cpu.userTime = 10;
        second.processes.push_back(restarted);

        ProcessSnapshotView const view = model.Update(second, _systemDelta(1000000, 1000000));

        ASSERT_EQ(view.processes.size(), 1u);
        EXPECT_TRUE(view.processes[0].ratesUnavailable);
    }

    TEST(ProcessModelTest, DerivesDiskRatesFromTransferCounters)
    {
        ProcessModel model(1);

        platform::ProcessSnapshot first;
        first.capturedAt = T0;
        first.processes.push_back(_makeProcess(100, CREATE_TIME_A));
        model.Update(first, _systemDelta(0, 0));

        platform::ProcessSnapshot second;
        second.capturedAt = T1;
        auto process = _makeProcess(100, CREATE_TIME_A);
        process.io.readTransferCount = 2048;
        process.io.writeTransferCount = 4096;
        second.processes.push_back(process);

        ProcessSnapshotView const view = model.Update(second, _systemDelta(1000000, 1000000));

        ASSERT_EQ(view.processes.size(), 1u);
        EXPECT_FALSE(view.processes[0].ratesUnavailable);
        // Both are per second, and the synthetic interval is exactly 1000 ms.
        EXPECT_DOUBLE_EQ(view.processes[0].diskReadBytesPerSec, 2048.0);
        EXPECT_DOUBLE_EQ(view.processes[0].diskWriteBytesPerSec, 4096.0);
    }

    TEST(ProcessModelTest, ResolvesParentIndices)
    {
        ProcessModel model(1);

        platform::ProcessSnapshot raw;
        raw.capturedAt = T0;
        raw.processes.push_back(_makeProcess(10, CREATE_TIME_A, 0, "parent.exe"));
        raw.processes.push_back(_makeProcess(20, CREATE_TIME_B, 10, "child.exe"));
        raw.processes.push_back(_makeProcess(30, CREATE_TIME_C, 999, "orphan.exe"));

        ProcessSnapshotView const view = model.Update(raw, _systemDelta(0, 0));

        ASSERT_EQ(view.processes.size(), 3u);
        EXPECT_EQ(view.processes[0].parentIndex, NO_PARENT_INDEX) << "the root has no parent in this snapshot";
        EXPECT_EQ(view.processes[1].parentIndex, 0u) << "the child's parent is at index 0";
        EXPECT_EQ(view.processes[2].parentIndex, NO_PARENT_INDEX) << "an absent parent must not resolve to a bogus index";
    }

    TEST(ProcessModelTest, ProcessIsNotItsOwnParent)
    {
        // Some system entries report a parent PID equal to their own.
        ProcessModel model(1);

        platform::ProcessSnapshot raw;
        raw.capturedAt = T0;
        raw.processes.push_back(_makeProcess(4, CREATE_TIME_A, 4, "System"));

        ProcessSnapshotView const view = model.Update(raw, _systemDelta(0, 0));

        ASSERT_EQ(view.processes.size(), 1u);
        EXPECT_EQ(view.processes[0].parentIndex, NO_PARENT_INDEX);
    }

    TEST(ProcessModelTest, DropsDepartedProcessesFromTracking)
    {
        // Tracking is keyed on identity and must not accumulate entries for
        // processes that have exited, or the map grows without bound.
        ProcessModel model(1);

        platform::ProcessSnapshot first;
        first.capturedAt = T0;
        first.processes.push_back(_makeProcess(100, CREATE_TIME_A));
        first.processes.push_back(_makeProcess(200, CREATE_TIME_B));
        model.Update(first, _systemDelta(0, 0));
        EXPECT_EQ(model.TrackedCount(), 2u);

        platform::ProcessSnapshot second;
        second.capturedAt = T1;
        second.processes.push_back(_makeProcess(100, CREATE_TIME_A));
        model.Update(second, _systemDelta(1000000, 1000000));

        EXPECT_EQ(model.TrackedCount(), 1u) << "the exited process must no longer be tracked";
    }

    TEST(ProcessModelTest, AggregatesThreadAndHandleCounts)
    {
        ProcessModel model(1);

        platform::ProcessSnapshot raw;
        raw.capturedAt = T0;
        auto first = _makeProcess(100, CREATE_TIME_A);
        first.threadCount = 3;
        first.handleCount = 20;
        auto second = _makeProcess(200, CREATE_TIME_B);
        second.threadCount = 4;
        second.handleCount = 30;
        raw.processes.push_back(first);
        raw.processes.push_back(second);

        ProcessSnapshotView const view = model.Update(raw, _systemDelta(0, 0));

        EXPECT_EQ(view.threadCount, 7u);
        EXPECT_EQ(view.handleCount, 50u);
    }

    TEST(ProcessModelTest, VersionIncrementsOnEveryUpdate)
    {
        // The UI detects change by version, so it must never repeat.
        ProcessModel model(1);

        platform::ProcessSnapshot raw;
        raw.capturedAt = T0;
        raw.processes.push_back(_makeProcess(100, CREATE_TIME_A));

        EXPECT_EQ(model.Update(raw, _systemDelta(0, 0)).version, 1u);
        EXPECT_EQ(model.Update(raw, _systemDelta(1, 1)).version, 2u);
        EXPECT_EQ(model.Update(raw, _systemDelta(1, 1)).version, 3u);
    }

    TEST(ProcessModelTest, EmptySnapshotIsHandled)
    {
        ProcessModel model(1);

        platform::ProcessSnapshot raw;
        raw.capturedAt = T0;

        ProcessSnapshotView const view = model.Update(raw, _systemDelta(0, 0));
        EXPECT_TRUE(view.processes.empty());
        EXPECT_EQ(view.threadCount, 0u);
    }

    // ------------------------------------------------------------------------
    // SystemModel
    // ------------------------------------------------------------------------

    namespace
    {
        platform::SystemCpuTimes _makeCpuTimes(uint64_t idle, uint64_t kernel, uint64_t user)
        {
            platform::SystemCpuTimes times;
            times.idleTime = idle;
            times.kernelTime = kernel;
            times.userTime = user;
            return times;
        }

        platform::SystemMemoryInfo _makeMemory(uint64_t total, uint64_t available)
        {
            platform::SystemMemoryInfo info;
            info.totalPhysical = total;
            info.availablePhysical = available;
            // The API reports load as a whole percentage; derive it so the two
            // fields stay consistent, matching real behaviour.
            info.memoryLoadPercent =
                (total == 0) ? 0u : static_cast<uint32_t>(((total - available) * 100) / total);
            return info;
        }
    }

    TEST(SystemModelTest, FirstSampleHasNoCpuRateButHasMemory)
    {
        SystemModel model(4, 1000, 60);

        // Kernel time includes idle on Windows, so idle is inside kernel here.
        model.Update(_makeCpuTimes(0, 10000000, 2000000), _makeMemory(16ull << 30, 8ull << 30), T0);

        SystemView const& view = model.Latest();
        EXPECT_TRUE(view.ratesUnavailable);
        EXPECT_DOUBLE_EQ(view.cpuPercent, 0.0);

        // Memory is instantaneous, so it is available from the first sample.
        EXPECT_DOUBLE_EQ(view.memoryUsedPercent, 50.0);
        EXPECT_EQ(view.memoryUsedBytes, 8ull << 30);
    }

    TEST(SystemModelTest, DerivesSystemCpuFromKernelMinusIdle)
    {
        SystemModel model(1, 1000, 60);

        // Baseline: 10 s kernel (8 s of it idle), 2 s user.
        model.Update(_makeCpuTimes(80000000, 100000000, 20000000), _makeMemory(1, 1), T0);

        // One second later: kernel +1 s (0.5 s idle), user +0.5 s.
        //   idle delta  = 5000000
        //   kernel busy = 10000000 - 5000000 = 5000000
        //   busy total  = 5000000 + 5000000 = 10000000
        //   total       = 10000000 + 5000000 = 15000000
        //   => 66.67%
        model.Update(_makeCpuTimes(85000000, 110000000, 25000000), _makeMemory(1, 1), T1);

        SystemView const& view = model.Latest();
        EXPECT_FALSE(view.ratesUnavailable);
        EXPECT_NEAR(view.cpuPercent, 66.6667, 0.01);
    }

    TEST(SystemModelTest, FullyIdleSystemReportsZero)
    {
        SystemModel model(1, 1000, 60);
        model.Update(_makeCpuTimes(1000000, 1000000, 0), _makeMemory(1, 1), T0);
        // Only idle time accrued, and kernel time grew by exactly that idle time.
        model.Update(_makeCpuTimes(2000000, 2000000, 0), _makeMemory(1, 1), T1);

        EXPECT_DOUBLE_EQ(model.Latest().cpuPercent, 0.0);
    }

    TEST(SystemModelTest, FullyBusySystemReportsHundred)
    {
        SystemModel model(1, 1000, 60);
        model.Update(_makeCpuTimes(0, 1000000, 0), _makeMemory(1, 1), T0);
        // Kernel grew with no idle, plus user time.
        model.Update(_makeCpuTimes(0, 2000000, 1000000), _makeMemory(1, 1), T1);

        EXPECT_DOUBLE_EQ(model.Latest().cpuPercent, 100.0);
    }

    TEST(SystemModelTest, CounterRollbackLeavesCpuUnavailable)
    {
        SystemModel model(1, 1000, 60);
        model.Update(_makeCpuTimes(5000000, 5000000, 5000000), _makeMemory(1, 1), T0);
        // A system-wide counter going backwards is implausible; do not report a
        // fabricated rate for it.
        model.Update(_makeCpuTimes(100, 100, 100), _makeMemory(1, 1), T1);

        EXPECT_TRUE(model.Latest().ratesUnavailable);
    }

    TEST(SystemModelTest, HistoryGrowsThenSaturatesAtCapacity)
    {
        SystemModel model(1, 1000, 60);
        size_t const capacity = model.HistoryCapacity();
        ASSERT_GT(capacity, 2u);

        // Push well past capacity; the series must stop growing.
        for (size_t i = 0; i < capacity * 3; ++i)
        {
            model.Update(_makeCpuTimes(i * 1000, i * 1000 + 500, i * 100), _makeMemory(100, 50), T0 + i * 1000000);
        }

        HistoryView const history = model.History();
        EXPECT_EQ(history.cpuTotal.size(), capacity);
        EXPECT_EQ(history.memoryUsed.size(), capacity);
    }

    TEST(SystemModelTest, HistoryIsChronological)
    {
        SystemModel model(1, 1000, 60);

        // First sample: memory half used. Then a quarter used.
        model.Update(_makeCpuTimes(0, 1000000, 0), _makeMemory(100, 50), T0);
        model.Update(_makeCpuTimes(1000000, 2000000, 0), _makeMemory(100, 75), T1);

        HistoryView const history = model.History();
        ASSERT_GE(history.memoryUsed.size(), 2u);
        // Oldest first: the earlier 50% sample precedes the later 75% one.
        EXPECT_DOUBLE_EQ(history.memoryUsed[0], 50.0);
        EXPECT_DOUBLE_EQ(history.memoryUsed[1], 25.0);
    }

    TEST(SystemModelTest, PerProcessorPercentagesAreDerived)
    {
        SystemModel model(2, 1000, 60);

        // Establish a baseline for two processors. Both go through Update, so the
        // aggregate advances alongside the per-core series.
        std::vector<platform::ProcessorCpuTimes> baseline(2);
        baseline[0] = platform::ProcessorCpuTimes{0, 1000000, 0, 0, 0};
        baseline[1] = platform::ProcessorCpuTimes{0, 1000000, 0, 0, 0};
        model.Update(_makeCpuTimes(0, 1000000, 0), _makeMemory(100, 50), T0, baseline);

        // Processor 0 fully busy, processor 1 fully idle.
        std::vector<platform::ProcessorCpuTimes> next(2);
        next[0] = platform::ProcessorCpuTimes{0, 2000000, 1000000, 0, 0};
        next[1] = platform::ProcessorCpuTimes{1000000, 2000000, 0, 0, 0};
        model.Update(_makeCpuTimes(0, 2000000, 0), _makeMemory(100, 50), T1, next);

        auto const& percentages = model.Latest().perProcessorCpuPercent;
        ASSERT_EQ(percentages.size(), 2u);
        EXPECT_DOUBLE_EQ(percentages[0], 100.0);
        EXPECT_DOUBLE_EQ(percentages[1], 0.0);
    }

    TEST(SystemModelTest, PerProcessorCountChangeResetsRates)
    {
        // Processor count is fixed on real hardware, but a stale vector must not be
        // differenced against a differently sized one.
        //
        // The published vector is sized to the model's own ring count rather than to
        // whatever the probe reported: the rings come from the topology and never change,
        // so a probe that disagrees with it cannot make the series ragged.
        SystemModel model(2, 1000, 60);

        std::vector<platform::ProcessorCpuTimes> two(2);
        model.Update(_makeCpuTimes(0, 1000000, 0), _makeMemory(100, 50), T0, two);

        // A probe reporting four processors against a two-processor model.
        std::vector<platform::ProcessorCpuTimes> four(4);
        four[0].kernelTime = 5000000;
        model.Update(_makeCpuTimes(0, 2000000, 0), _makeMemory(100, 50), T1, four);

        auto const& percentages = model.Latest().perProcessorCpuPercent;
        EXPECT_EQ(percentages.size(), 2u) << "the published count follows the topology, not the probe";
        for (double value : percentages)
        {
            EXPECT_DOUBLE_EQ(value, 0.0) << "no baseline exists for the new processor set";
        }
    }

    TEST(SystemModelTest, PerProcessorHistoryIsPopulatedPerCore)
    {
        // The per-core chart grid needs a history series per logical processor, not just
        // current values. This pins that History() carries them, because an empty series
        // renders as a blank chart -- which looks identical to a chart that has data but
        // no size, so the two must be told apart here rather than on screen.
        constexpr uint32_t CORES = 4;
        SystemModel model(CORES, 1000, 60);

        // Four samples, each through the single Update entry point so the aggregate and
        // the per-core series advance together.
        for (uint32_t sample = 0; sample < 4; ++sample)
        {
            std::vector<platform::ProcessorCpuTimes> times(CORES);
            for (uint32_t core = 0; core < CORES; ++core)
            {
                uint64_t const busy = static_cast<uint64_t>(sample) * 1000000 * (core + 1);
                uint64_t const idle = static_cast<uint64_t>(sample) * 1000000 * (CORES - core);
                times[core].kernelTime = 1000000 + busy;
                times[core].userTime = busy;
                times[core].idleTime = idle;
            }

            model.Update(_makeCpuTimes(sample * 1000000, (sample + 1) * 1000000, 0),
                         _makeMemory(100, 50),
                         T0 + (sample * 1000000),
                         times);
        }

        HistoryView const history = model.History();

        // One series per logical processor, which is what the grid indexes.
        ASSERT_EQ(history.perProcessorCpu.size(), CORES)
            << "History() must carry one series per logical processor";

        // Every series must be the same length as the aggregate, so the grid can index
        // them in step and the time axes line up.
        for (size_t core = 0; core < history.perProcessorCpu.size(); ++core)
        {
            EXPECT_EQ(history.perProcessorCpu[core].size(), history.cpuTotal.size())
                << "core " << core << " series length differs from the aggregate";
            EXPECT_GE(history.perProcessorCpu[core].size(), 2u)
                << "core " << core << " has too few points for a chart to draw a line";
        }
    }

    TEST(SystemModelTest, PerProcessorHistoryKeepsLengthWhenTheProbeFails)
    {
        // If a probe failure skipped pushing to the per-core rings, their series would
        // fall behind the aggregate and the grid would plot the wrong samples against the
        // axis. The invariant is that every series stays the same length.
        //
        // This is the defect that made every core chart blank: the per-core series held
        // four points while the aggregate held none, because the two were advanced by
        // separate calls and the per-core one was skipped when its probe failed.
        constexpr uint32_t CORES = 2;
        SystemModel model(CORES, 1000, 60);

        std::vector<platform::ProcessorCpuTimes> times(CORES);
        times[0].kernelTime = 1000000;
        times[1].kernelTime = 1000000;
        model.Update(_makeCpuTimes(0, 1000000, 0), _makeMemory(100, 50), T0, times);

        // A failed per-processor read arrives as an empty vector.
        model.Update(_makeCpuTimes(0, 2000000, 0), _makeMemory(100, 50), T1, {});

        // And one more healthy sample.
        times[0].kernelTime = 3000000;
        times[1].kernelTime = 3000000;
        model.Update(_makeCpuTimes(0, 3000000, 0), _makeMemory(100, 50), T1 + 1000000, times);

        HistoryView const history = model.History();

        ASSERT_EQ(history.perProcessorCpu.size(), CORES);
        ASSERT_GE(history.cpuTotal.size(), 3u) << "three samples were taken";
        for (size_t core = 0; core < history.perProcessorCpu.size(); ++core)
        {
            EXPECT_EQ(history.perProcessorCpu[core].size(), history.cpuTotal.size())
                << "a failed per-processor read must not desynchronise core " << core;
        }
    }
    TEST(SystemModelTest, PerProcessorHistoryIsBounded)
    {
        // A resident monitor must not accumulate history without bound; the per-core
        // rings are subject to the same capacity as the aggregate.
        SystemModel model(2, 1000, 60);
        size_t const capacity = model.HistoryCapacity();
        ASSERT_GT(capacity, 2u);

        for (size_t i = 0; i < capacity * 3; ++i)
        {
            std::vector<platform::ProcessorCpuTimes> times(2);
            times[0].kernelTime = 1000000 + (i * 1000);
            times[1].kernelTime = 1000000 + (i * 500);
            model.UpdatePerProcessor(times);
        }

        HistoryView const history = model.History();
        ASSERT_EQ(history.perProcessorCpu.size(), 2u);
        for (auto const& series : history.perProcessorCpu)
        {
            EXPECT_EQ(series.size(), capacity);
        }
    }

    TEST(SystemModelTest, ProcessorInfoIsRetained)
    {
        SystemModel model(4, 1000, 60);

        platform::SystemProcessorInfo info;
        info.logicalProcessorCount = 8;
        info.physicalCoreCount = 4;
        info.architecture = "x64";
        model.SetProcessorInfo(info);

        EXPECT_EQ(model.Latest().processor.logicalProcessorCount, 8u);
        EXPECT_EQ(model.Latest().processor.physicalCoreCount, 4u);
        EXPECT_EQ(model.Latest().processor.architecture, "x64");
    }

    TEST(SystemModelTest, VersionIncrementsOnEveryUpdate)
    {
        SystemModel model(1, 1000, 60);
        model.Update(_makeCpuTimes(0, 0, 0), _makeMemory(1, 1), T0);
        uint64_t const first = model.Latest().version;
        model.Update(_makeCpuTimes(1000, 1000, 1000), _makeMemory(1, 1), T1);
        EXPECT_GT(model.Latest().version, first);
    }
}
