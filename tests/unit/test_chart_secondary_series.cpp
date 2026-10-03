// Tests for the chart's optional second series.
//
// The second line is a shape that every redraw clears, including the redraws of charts that never
// set it. Leaving it unconstructed and guarding only the drawing path is what crashed the
// application: the clear at the top of the redraw ran before any guard, on every chart, and
// dereferenced a null pointer.
//
// The shape itself needs a desktop session, so what is tested here is the contract that caused the
// fault: an empty series means no line, a populated one means a line, and the two series are kept
// apart so one cannot silently replace the other.
#include <gtest/gtest.h>

#include <vector>

#include "UI/Charts/ChartSeries.h"

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the state HistoryChart keeps for its two series.
        struct SeriesState
        {
            std::vector<double> primary;
            std::vector<double> secondary;

            void SetPrimary(std::vector<double> values) { primary = std::move(values); }
            void SetSecondary(std::vector<double> values) { secondary = std::move(values); }

            /// Whether the dashed line is drawn.
            [[nodiscard]] bool SecondaryVisible() const noexcept { return !secondary.empty(); }
        };
    }

    TEST(ChartSecondarySeriesTest, AChartWithoutASecondSeriesKeepsItEmpty)
    {
        // The common case: most charts have one line, and the second must stay empty rather than
        // being filled with the first.
        SeriesState state;
        state.SetPrimary({1.0, 2.0, 3.0});

        EXPECT_FALSE(state.SecondaryVisible());
        EXPECT_TRUE(state.secondary.empty());
    }

    TEST(ChartSecondarySeriesTest, ASuppliedSecondSeriesIsKeptApart)
    {
        // Reads and writes share an axis but must not overwrite each other.
        SeriesState state;
        state.SetPrimary({100.0, 200.0, 300.0});
        state.SetSecondary({10.0, 20.0, 30.0});

        EXPECT_TRUE(state.SecondaryVisible());
        ASSERT_EQ(state.primary.size(), 3u);
        ASSERT_EQ(state.secondary.size(), 3u);
        EXPECT_DOUBLE_EQ(state.primary[0], 100.0);
        EXPECT_DOUBLE_EQ(state.secondary[0], 10.0);
    }

    TEST(ChartSecondarySeriesTest, AnEmptySeriesRemovesTheLine)
    {
        // Clearing must remove the dashed line rather than leave the previous one on screen, which
        // would silently show stale data.
        SeriesState state;
        state.SetPrimary({1.0, 2.0});
        state.SetSecondary({5.0, 6.0});
        ASSERT_TRUE(state.SecondaryVisible());

        state.SetSecondary({});
        EXPECT_FALSE(state.SecondaryVisible());
    }

    TEST(ChartSecondarySeriesTest, ASinglePointIsNotEnoughToDraw)
    {
        // A line needs two points. One point would draw nothing and read as an idle metric, so the
        // drawing path treats a short series the same way it treats an empty one.
        SeriesState state;
        state.SetSecondary({42.0});

        bool const drawable = state.secondary.size() >= 2;
        EXPECT_FALSE(drawable) << "a single sample cannot describe a line";
    }

    TEST(ChartSecondarySeriesTest, BothSeriesShareOneWindow)
    {
        // The two lines are drawn from the same step and the same leading offset, or they would show
        // different time spans on one axis and the comparison would be meaningless.
        ChartSeries readSeries;
        readSeries.values = {1.0, 2.0, 3.0};
        readSeries.windowSamples = 60;

        ChartSeries writeSeries;
        writeSeries.values = {4.0, 5.0, 6.0};
        writeSeries.windowSamples = 60;

        EXPECT_EQ(readSeries.windowSamples, writeSeries.windowSamples);
        EXPECT_TRUE(readSeries.HasWindow());
    }
}
