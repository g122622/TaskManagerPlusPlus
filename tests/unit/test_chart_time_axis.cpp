// Tests for the chart's time axis.
//
// The axis is a fixed, right-anchored window: the newest sample sits at the right edge and
// older samples extend leftwards, filling the window as history accumulates. Three things
// can go wrong here and none of them shows up as an error, only as a chart that reads
// wrongly:
//
//   * fitting the data to the width, so a few samples appear to be a settled history;
//   * anchoring on the left, so the chart looks like it is losing data as space grows on
//     the right;
//   * getting the leading offset wrong, so the newest sample is not on the right edge.
//
// These tests pin the arithmetic for all three.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the x-position calculation in HistoryChart::_redraw and Sparkline::_redraw.
        ///
        /// Duplicated rather than exposed, because both classes need WinRT types to
        /// instantiate. Keeping it trivial is deliberate: it makes the two comparable by eye,
        /// which matters because an off-by-one here shifts every point on every chart.
        struct PlotGeometry
        {
            double step{0.0};
            double leadingGap{0.0};
            double firstX{0.0};
            double lastX{0.0};
        };

        [[nodiscard]] PlotGeometry ComputeGeometry(size_t sampleCount, size_t windowSamples, double width)
        {
            PlotGeometry geometry;
            if (sampleCount < 2 || width <= 0.0)
            {
                return geometry;
            }

            size_t const span = (windowSamples > 1) ? windowSamples : sampleCount;
            geometry.step = (span > 1) ? (width / static_cast<double>(span - 1)) : width;

            // The window is right-anchored, so the line only starts partway across when the
            // data is shorter than the window.
            geometry.leadingGap =
                (span > sampleCount) ? (width - (static_cast<double>(sampleCount - 1) * geometry.step)) : 0.0;

            geometry.firstX = geometry.leadingGap;
            geometry.lastX = geometry.leadingGap + (static_cast<double>(sampleCount - 1) * geometry.step);

            return geometry;
        }
    }

    // ------------------------------------------------------------------------
    // Right anchoring
    // ------------------------------------------------------------------------

    TEST(ChartTimeAxisTest, NewestSampleSitsOnTheRightEdge)
    {
        // This is the property that makes the chart read correctly: however much history
        // exists, the latest reading is always at the right-hand edge and older data extends
        // to its left.
        for (size_t samples = 2; samples <= 60; ++samples)
        {
            PlotGeometry const geometry = ComputeGeometry(samples, 60, 600.0);

            EXPECT_NEAR(geometry.lastX, 600.0, 1e-9)
                << "with " << samples << " samples the newest must be at the right edge";
        }
    }

    TEST(ChartTimeAxisTest, FewSamplesOccupyOnlyTheRightHandPart)
    {
        // Three of sixty samples cover about 2/59 of the width, all of it on the right.
        PlotGeometry const geometry = ComputeGeometry(3, 60, 600.0);

        EXPECT_GT(geometry.leadingGap, 570.0) << "a nearly empty window must leave its left part empty";
        EXPECT_NEAR(geometry.firstX, geometry.leadingGap, 1e-9);
        EXPECT_NEAR(geometry.lastX, 600.0, 1e-9);
    }

    TEST(ChartTimeAxisTest, TheLineGrowsLeftwardsAsSamplesArrive)
    {
        // The oldest sample's position must move left monotonically, so the chart visibly
        // fills towards the left rather than appearing to shift or compress.
        double previousFirstX = 1.0e30;
        for (size_t samples = 2; samples <= 60; ++samples)
        {
            double const firstX = ComputeGeometry(samples, 60, 600.0).firstX;

            EXPECT_LT(firstX, previousFirstX) << "the oldest sample must move left at " << samples << " samples";
            previousFirstX = firstX;
        }
    }

    TEST(ChartTimeAxisTest, AFullWindowSpansTheWholeWidth)
    {
        // Once the window is full there is no leading gap and the line covers the width, so
        // it can scroll.
        PlotGeometry const geometry = ComputeGeometry(60, 60, 600.0);

        EXPECT_DOUBLE_EQ(geometry.leadingGap, 0.0);
        EXPECT_DOUBLE_EQ(geometry.firstX, 0.0);
        EXPECT_DOUBLE_EQ(geometry.lastX, 600.0);
    }

    TEST(ChartTimeAxisTest, LeftAnchoringIsNotUsed)
    {
        // The behaviour this replaced put the first sample at x = 0, which made the chart
        // look like it was losing data as the empty space grew on the right. This asserts the
        // regression cannot return.
        PlotGeometry const geometry = ComputeGeometry(5, 60, 600.0);

        EXPECT_GT(geometry.firstX, 0.0)
            << "a partly filled window must not start at the left edge; that was the bug";
    }

    TEST(ChartTimeAxisTest, FittingTheDataToTheWidthIsNotUsed)
    {
        // The other rejected behaviour: three samples drawn across the whole chart, which
        // reads as a settled history that does not exist yet.
        PlotGeometry const geometry = ComputeGeometry(3, 60, 600.0);

        EXPECT_LT(geometry.lastX - geometry.firstX, 30.0)
            << "three samples of a sixty-sample window must occupy far less than the full width";
    }

    // ------------------------------------------------------------------------
    // Edge cases
    // ------------------------------------------------------------------------

    TEST(ChartTimeAxisTest, WithoutAWindowTheSamplesFillTheWidth)
    {
        // A sparkline with no axis has no time to represent, so it shows the shape of what
        // exists across the whole width.
        PlotGeometry const geometry = ComputeGeometry(10, 0, 600.0);

        EXPECT_DOUBLE_EQ(geometry.leadingGap, 0.0);
        EXPECT_DOUBLE_EQ(geometry.step, 600.0 / 9.0);
        EXPECT_DOUBLE_EQ(geometry.lastX, 600.0);
    }

    TEST(ChartTimeAxisTest, TwoSamplesSpanTheWindowWithoutDividingByZero)
    {
        // The step is width / (span - 1), so a span of one would divide by zero. Two is the
        // smallest usable count and must produce a sane step.
        PlotGeometry const windowed = ComputeGeometry(2, 60, 600.0);
        EXPECT_GT(windowed.step, 0.0);
        EXPECT_DOUBLE_EQ(windowed.lastX, 600.0);

        // Two samples with no window is the degenerate fit-the-data case.
        PlotGeometry const fitted = ComputeGeometry(2, 0, 600.0);
        EXPECT_DOUBLE_EQ(fitted.leadingGap, 0.0);
        EXPECT_DOUBLE_EQ(fitted.lastX, 600.0);
    }

    TEST(ChartTimeAxisTest, SingleSampleProducesNoGeometry)
    {
        // One point cannot describe a line, so nothing is drawn rather than a step being
        // produced from a subtraction underflow.
        PlotGeometry const geometry = ComputeGeometry(1, 60, 600.0);
        EXPECT_DOUBLE_EQ(geometry.step, 0.0);
        EXPECT_DOUBLE_EQ(geometry.lastX, 0.0);
    }

    TEST(ChartTimeAxisTest, ZeroWidthProducesNoGeometry)
    {
        // Before the first layout pass the canvas has no width.
        PlotGeometry const geometry = ComputeGeometry(10, 60, 0.0);
        EXPECT_DOUBLE_EQ(geometry.step, 0.0);
    }
}
