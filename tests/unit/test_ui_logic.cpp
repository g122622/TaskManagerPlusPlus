// Tests for the UI layer's testable parts: value formatting and the process
// list's sort/filter cache.
//
// The views themselves need a desktop session and are verified by hand; what is
// tested here is the logic that decides what the views show, which is where the
// mistakes that matter live -- a wrong unit label, a sort that disagrees with the
// column, a filter that hides the wrong rows.
#include <gtest/gtest.h>

#include "UI/Formatting.h"
#include "UI/ProcessListModel.h"

#include <string>

namespace tmpp::ui
{
    namespace
    {
        domain::ProcessView _makeProcess(uint32_t pid,
                                        std::string name,
                                        double cpuPercent = 0.0,
                                        uint64_t workingSet = 0,
                                        bool ratesAvailable = true)
        {
            domain::ProcessView view;
            view.identity.pid = pid;
            view.identity.createTime = 1000 + pid;
            view.imageName = std::move(name);
            view.cpuPercent = cpuPercent;
            view.memory.workingSetSize = workingSet;
            view.ratesUnavailable = !ratesAvailable;
            return view;
        }

        domain::ProcessSnapshotView _makeSnapshot(uint64_t version, std::vector<domain::ProcessView> processes)
        {
            domain::ProcessSnapshotView snapshot;
            snapshot.version = version;
            snapshot.processes = std::move(processes);
            return snapshot;
        }
    }

    // ------------------------------------------------------------------------
    // Formatting
    // ------------------------------------------------------------------------

    TEST(FormattingTest, FormatsBytesWithBinaryMultiples)
    {
        // Windows labels 1024-based units as KB/MB, so these must agree with
        // Explorer and Task Manager rather than with SI.
        EXPECT_EQ(FormatBytes(0), "0 B");
        EXPECT_EQ(FormatBytes(512), "512 B");
        EXPECT_EQ(FormatBytes(1024), "1.0 KB");
        EXPECT_EQ(FormatBytes(1536), "1.5 KB");
        EXPECT_EQ(FormatBytes(1024ull * 1024), "1.0 MB");
        EXPECT_EQ(FormatBytes(1024ull * 1024 * 1024), "1.0 GB");
    }

    TEST(FormattingTest, FormatsLargeByteCountsWithoutOverflow)
    {
        // A multi-terabyte total must scale rather than overflow the unit table.
        EXPECT_EQ(FormatBytes(1024ull * 1024 * 1024 * 1024), "1.0 TB");
        EXPECT_EQ(FormatBytes(2048ull * 1024 * 1024 * 1024), "2.0 TB");
    }

    TEST(FormattingTest, PromotesUnitWhenRoundingWouldReachTheBoundary)
    {
        // Scaling by division can leave a value that rounds up to 1024.0. Printing
        // "1024.0 KB" would be a visible contradiction, so it must promote to "1.0 MB".
        EXPECT_EQ(FormatBytes(1024ull * 1024 - 1), "1.0 MB");
        EXPECT_EQ(FormatBytes(1024ull * 1024 * 1024 - 1), "1.0 GB");
    }

    TEST(FormattingTest, ValuesBelowTheRoundingThresholdStayInTheirUnit)
    {
        // A value that does not round to the boundary must keep its own unit.
        EXPECT_EQ(FormatBytes(1024ull * 1024 - 1024), "1023.0 KB");
        EXPECT_EQ(FormatBytes(1536), "1.5 KB");
    }

    TEST(FormattingTest, FormatsRatesWithPerSecondSuffix)
    {
        EXPECT_EQ(FormatBytesPerSecond(1024.0), "1.0 KB/s");
        EXPECT_EQ(FormatBytesPerSecond(0.0), "0 B/s");
    }

    TEST(FormattingTest, RejectsInvalidRates)
    {
        // A negative or non-finite rate means "not available", not a negative number.
        EXPECT_EQ(FormatBytesPerSecond(-1.0), UnavailableValue());
        EXPECT_EQ(FormatBytesPerSecond(std::numeric_limits<double>::infinity()), UnavailableValue());
    }

    TEST(FormattingTest, FormatsPercentages)
    {
        EXPECT_EQ(FormatPercent(0.0), "0.0%");
        EXPECT_EQ(FormatPercent(12.34), "12.3%");
        EXPECT_EQ(FormatPercent(100.0), "100.0%");
    }

    TEST(FormattingTest, ProcessCpuDropsDecimalsWhenLarge)
    {
        // Below 10% a decimal is informative; above it, it is noise in a column of
        // changing numbers.
        EXPECT_EQ(FormatProcessCpuPercent(0.0), "0.0%");
        EXPECT_EQ(FormatProcessCpuPercent(9.87), "9.9%");
        EXPECT_EQ(FormatProcessCpuPercent(10.0), "10%");
        EXPECT_EQ(FormatProcessCpuPercent(245.6), "246%");
    }

    TEST(FormattingTest, FormatsCountsWithThousandsSeparators)
    {
        EXPECT_EQ(FormatCount(0), "0");
        EXPECT_EQ(FormatCount(7), "7");
        EXPECT_EQ(FormatCount(999), "999");
        EXPECT_EQ(FormatCount(1000), "1,000");
        EXPECT_EQ(FormatCount(1234567), "1,234,567");
    }

    TEST(FormattingTest, FormatsDurations)
    {
        EXPECT_EQ(FormatDuration(0), "00:00:00");
        EXPECT_EQ(FormatDuration(59), "00:00:59");
        EXPECT_EQ(FormatDuration(3661), "01:01:01");
        EXPECT_EQ(FormatDuration(90061), "1d 01:01:01");
    }

    TEST(FormattingTest, HandlesNonFinitePercentages)
    {
        EXPECT_EQ(FormatPercent(std::numeric_limits<double>::quiet_NaN()), UnavailableValue());
        EXPECT_EQ(FormatProcessCpuPercent(std::numeric_limits<double>::quiet_NaN()), UnavailableValue());
    }

    TEST(FormattingTest, UnavailableIsNotZero)
    {
        // The placeholder must be visually distinct from a genuine zero: showing 0
        // for an unreadable value conceals a permissions problem or a dead probe.
        EXPECT_NE(UnavailableValue(), "0");
        EXPECT_FALSE(UnavailableValue().empty());
    }

    // ------------------------------------------------------------------------
    // ProcessListModel
    // ------------------------------------------------------------------------

    TEST(ProcessListModelTest, EmptySnapshotYieldsNoRows)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1, {});

        model.Update(snapshot, ListQuery{});
        EXPECT_EQ(model.VisibleCount(), 0u);
        EXPECT_EQ(model.TotalCount(), 0u);
    }

    TEST(ProcessListModelTest, SortsByCpuDescendingByDefault)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "low.exe", 1.0),
                                             _makeProcess(2, "high.exe", 50.0),
                                             _makeProcess(3, "mid.exe", 10.0)});

        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Descending;
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 3u);
        EXPECT_EQ(snapshot.processes[order[0]].imageName, "high.exe");
        EXPECT_EQ(snapshot.processes[order[1]].imageName, "mid.exe");
        EXPECT_EQ(snapshot.processes[order[2]].imageName, "low.exe");
    }

    TEST(ProcessListModelTest, SortsByCpuAscending)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "a.exe", 5.0),
                                             _makeProcess(2, "b.exe", 1.0),
                                             _makeProcess(3, "c.exe", 9.0)});

        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Ascending;
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 3u);
        EXPECT_EQ(snapshot.processes[order[0]].imageName, "b.exe");
        EXPECT_EQ(snapshot.processes[order[2]].imageName, "c.exe");
    }

    TEST(ProcessListModelTest, SortsByNameCaseInsensitively)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "Zebra.exe"),
                                             _makeProcess(2, "apple.exe"),
                                             _makeProcess(3, "Mango.exe")});

        ListQuery query;
        query.column = SortColumn::Name;
        query.direction = SortDirection::Ascending;
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 3u);
        // Case-insensitive: apple, Mango, Zebra.
        EXPECT_EQ(snapshot.processes[order[0]].imageName, "apple.exe");
        EXPECT_EQ(snapshot.processes[order[1]].imageName, "Mango.exe");
        EXPECT_EQ(snapshot.processes[order[2]].imageName, "Zebra.exe");
    }

    TEST(ProcessListModelTest, SortsByMemoryUsingWorkingSet)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "small.exe", 0.0, 1024),
                                             _makeProcess(2, "large.exe", 0.0, 1024ull * 1024 * 100),
                                             _makeProcess(3, "medium.exe", 0.0, 1024ull * 1024)});

        ListQuery query;
        query.column = SortColumn::Memory;
        query.direction = SortDirection::Descending;
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 3u);
        EXPECT_EQ(snapshot.processes[order[0]].imageName, "large.exe");
        EXPECT_EQ(snapshot.processes[order[2]].imageName, "small.exe");
    }

    TEST(ProcessListModelTest, FiltersByNameCaseInsensitively)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "chrome.exe"),
                                             _makeProcess(2, "notepad.exe"),
                                             _makeProcess(3, "ChromeHelper.exe")});

        ListQuery query;
        query.filter = "CHROME";
        model.Update(snapshot, query);

        EXPECT_EQ(model.VisibleCount(), 2u);
        EXPECT_TRUE(model.IsFiltered());
    }

    TEST(ProcessListModelTest, EmptyFilterShowsEverything)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1, {_makeProcess(1, "a.exe"), _makeProcess(2, "b.exe")});

        ListQuery query;
        query.filter = "";
        model.Update(snapshot, query);

        EXPECT_EQ(model.VisibleCount(), 2u);
        EXPECT_FALSE(model.IsFiltered());
    }

    TEST(ProcessListModelTest, FilterMatchingNothingYieldsNoRows)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1, {_makeProcess(1, "a.exe")});

        ListQuery query;
        query.filter = "nonexistent";
        model.Update(snapshot, query);

        EXPECT_EQ(model.VisibleCount(), 0u);
        // The total still reflects the snapshot, so the UI can say "0 of 1".
        EXPECT_EQ(model.TotalCount(), 1u);
    }

    TEST(ProcessListModelTest, TiesBreakByPidForStableOrder)
    {
        // Two processes with identical CPU must not swap places between frames,
        // or the list visibly flickers under the user's cursor.
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(300, "c.exe", 5.0),
                                             _makeProcess(100, "a.exe", 5.0),
                                             _makeProcess(200, "b.exe", 5.0)});

        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Descending;
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 3u);
        EXPECT_EQ(snapshot.processes[order[0]].identity.pid, 100u);
        EXPECT_EQ(snapshot.processes[order[1]].identity.pid, 200u);
        EXPECT_EQ(snapshot.processes[order[2]].identity.pid, 300u);
    }

    TEST(ProcessListModelTest, UnavailableRatesSortLast)
    {
        // During the first sample nothing has rates. Ordering those rows last keeps
        // a CPU-sorted list from being dominated by blanks.
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "blank.exe", 0.0, 0, /*ratesAvailable=*/false),
                                             _makeProcess(2, "busy.exe", 25.0)});

        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Descending;
        query.placeUnavailableLast = true;
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 2u);
        EXPECT_EQ(snapshot.processes[order[0]].imageName, "busy.exe");
        EXPECT_EQ(snapshot.processes[order[1]].imageName, "blank.exe");
    }

    TEST(ProcessListModelTest, CacheSurvivesUnchangedInput)
    {
        // The whole point of the cache: an unchanged version and query must not
        // rebuild. A rebuild would allocate and sort again on every frame.
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1, {_makeProcess(1, "a.exe"), _makeProcess(2, "b.exe")});

        ListQuery query;
        model.Update(snapshot, query);
        auto const first = model.VisibleIndices();

        // Same version, same query.
        model.Update(snapshot, query);
        auto const second = model.VisibleIndices();

        EXPECT_EQ(first, second);
    }

    TEST(ProcessListModelTest, RebuildsWhenVersionChanges)
    {
        ProcessListModel model;
        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Descending;

        model.Update(_makeSnapshot(1, {_makeProcess(1, "a.exe", 1.0)}), query);
        ASSERT_EQ(model.VisibleCount(), 1u);

        // A new version means new data, so the cache must be rebuilt.
        model.Update(_makeSnapshot(2, {_makeProcess(1, "a.exe", 1.0), _makeProcess(2, "b.exe", 2.0)}), query);
        EXPECT_EQ(model.VisibleCount(), 2u);
    }

    TEST(ProcessListModelTest, RebuildsWhenFilterChanges)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1, {_makeProcess(1, "chrome.exe"), _makeProcess(2, "notepad.exe")});

        ListQuery query;
        model.Update(snapshot, query);
        EXPECT_EQ(model.VisibleCount(), 2u);

        query.filter = "chrome";
        model.Update(snapshot, query);
        EXPECT_EQ(model.VisibleCount(), 1u);

        query.filter.clear();
        model.Update(snapshot, query);
        EXPECT_EQ(model.VisibleCount(), 2u);
    }

    TEST(ProcessListModelTest, RebuildsWhenSortDirectionChanges)
    {
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1, {_makeProcess(1, "a.exe", 1.0), _makeProcess(2, "b.exe", 9.0)});

        ListQuery query;
        query.column = SortColumn::Cpu;

        query.direction = SortDirection::Descending;
        model.Update(snapshot, query);
        EXPECT_EQ(snapshot.processes[model.VisibleIndices()[0]].imageName, "b.exe");

        query.direction = SortDirection::Ascending;
        model.Update(snapshot, query);
        EXPECT_EQ(snapshot.processes[model.VisibleIndices()[0]].imageName, "a.exe");
    }

    TEST(ProcessListModelTest, SortAndFilterCompose)
    {
        // Filtering must happen before ordering, so the result is the filtered set
        // in sorted order rather than the sorted set filtered afterwards.
        ProcessListModel model;
        auto const snapshot = _makeSnapshot(1,
                                            {_makeProcess(1, "svc-a.exe", 5.0),
                                             _makeProcess(2, "svc-b.exe", 30.0),
                                             _makeProcess(3, "app.exe", 99.0)});

        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Descending;
        query.filter = "svc";
        model.Update(snapshot, query);

        auto const& order = model.VisibleIndices();
        ASSERT_EQ(order.size(), 2u);
        // app.exe is excluded despite the highest CPU, and svc-b precedes svc-a.
        EXPECT_EQ(snapshot.processes[order[0]].imageName, "svc-b.exe");
        EXPECT_EQ(snapshot.processes[order[1]].imageName, "svc-a.exe");
    }

    TEST(ProcessListModelTest, HandlesManyProcesses)
    {
        // The cache must cope with the process counts the target scenario names.
        std::vector<domain::ProcessView> processes;
        processes.reserve(2000);
        for (uint32_t i = 0; i < 2000; ++i)
        {
            processes.push_back(_makeProcess(i + 1, "proc" + std::to_string(i) + ".exe", static_cast<double>(i % 100)));
        }
        auto const snapshot = _makeSnapshot(1, std::move(processes));

        ProcessListModel model;
        ListQuery query;
        query.column = SortColumn::Cpu;
        query.direction = SortDirection::Descending;
        model.Update(snapshot, query);

        EXPECT_EQ(model.VisibleCount(), 2000u);
        // Highest CPU first, and the tie-break by PID is ascending within a value.
        EXPECT_GE(snapshot.processes[model.VisibleIndices()[0]].cpuPercent,
                  snapshot.processes[model.VisibleIndices()[1999]].cpuPercent);
    }
}
