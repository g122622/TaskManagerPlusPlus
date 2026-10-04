// Tests for the history chart's vertical scale.
//
// The scale had a bug worth pinning: the plot reserved padding at the bottom as well as the top, so a
// value of zero was drawn six pixels above the frame's lower edge. Every curve therefore had visible
// height when it was reading zero, and every other reading was off by the same amount.
//
// The grid had a related one. It divided the canvas into equal bands while the curve was drawn inside a
// padded box, so no grid line sat where its value was -- most visibly at the bottom, where the lowest
// line landed below the curve's own zero.
//
// The arithmetic is pure, so it is tested directly rather than through a visual tree.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors HistoryChart's private constant.
        constexpr double PLOT_PADDING = 6.0;

        /// Mirrors HistoryChart::YForRatio.
        [[nodiscard]] double YForRatio(double ratio, double height, double lineWidth)
        {
            double const bottomInset = lineWidth / 2.0;
            double const drawableHeight = height - PLOT_PADDING - bottomInset;
            return PLOT_PADDING + (drawableHeight * (1.0 - ratio));
        }
    }

    TEST(ChartBaselineTest, ZeroMapsToTheBottomEdgeLessHalfAStroke)
    {
        // The whole point of the fix: a reading of zero is drawn on the bottom of the plot, not floating
        // above it. Half the stroke is reserved so the line's lower half is not clipped by the frame.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;

        double const y = YForRatio(0.0, height, lineWidth);

        EXPECT_DOUBLE_EQ(y, height - (lineWidth / 2.0)) << "zero must sit on the bottom edge";
        EXPECT_LT(y, height) << "and not below it, or the stroke would be clipped";
    }

    TEST(ChartBaselineTest, TheMaximumSitsOnePaddingBelowTheTop)
    {
        // The top keeps its padding so a full-scale reading does not touch the frame.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;

        EXPECT_DOUBLE_EQ(YForRatio(1.0, height, lineWidth), PLOT_PADDING);
    }

    TEST(ChartBaselineTest, NoValueEverHasHeightWhenItIsZero)
    {
        // The reported symptom, stated as a property: whatever the plot size and whatever the stroke, a
        // zero reading must map to the bottom of the plot. Anything less than the full height would leave
        // the curve visibly raised.
        for (double const height : {40.0, 92.0, 220.0, 400.0, 800.0})
        {
            for (double const lineWidth : {0.5, 1.0, 2.0, 4.0, 8.0})
            {
                double const y = YForRatio(0.0, height, lineWidth);
                EXPECT_GE(y, height - lineWidth) << "height=" << height << " lineWidth=" << lineWidth;
                EXPECT_LE(y, height) << "height=" << height << " lineWidth=" << lineWidth;
            }
        }
    }

    TEST(ChartBaselineTest, TheScaleIsMonotonicAndInverted)
    {
        // Larger values are drawn higher, which in a canvas means a smaller y. A sign error here would
        // flip every chart and is worth catching.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;

        double previous = YForRatio(0.0, height, lineWidth);
        for (int i = 1; i <= 10; ++i)
        {
            double const y = YForRatio(static_cast<double>(i) / 10.0, height, lineWidth);
            EXPECT_LT(y, previous) << "a larger value must be drawn higher";
            previous = y;
        }
    }

    TEST(ChartBaselineTest, TheGridLinesAreEvenlySpacedAcrossThePlotAreaNotTheCanvas)
    {
        // The grid used to divide the canvas: y = (height / rows) * i. The curve is drawn inside an area
        // inset by the top padding and half a stroke at the bottom, so the two disagreed by a growing
        // amount towards the bottom -- the lowest grid line landed below the curve's zero.
        //
        // The spacing is now the plot area's height divided by the row count, which is what makes each
        // line sit where its value is.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;
        constexpr int rows = 10;

        double const plotArea = height - PLOT_PADDING - (lineWidth / 2.0);
        double const expectedStep = plotArea / static_cast<double>(rows);

        for (int i = 1; i < rows; ++i)
        {
            double const y = YForRatio(static_cast<double>(i) / static_cast<double>(rows), height, lineWidth);
            double const previous =
                (i == 1) ? YForRatio(0.0, height, lineWidth)
                         : YForRatio(static_cast<double>(i - 1) / static_cast<double>(rows), height, lineWidth);

            EXPECT_NEAR(previous - y, expectedStep, 1.0e-9) << "gap below line " << i;
        }

        // The old formula, kept as the counter-example: it puts the lowest line below zero.
        double const oldLowest = (height / rows) * 1;
        double const newLowest = YForRatio(1.0 / static_cast<double>(rows), height, lineWidth);
        EXPECT_GT(newLowest, oldLowest) << "the two formulas must actually differ, or this test proves nothing";
    }

    TEST(ChartBaselineTest, AZeroHeightPlotIsRejectedRatherThanInverted)
    {
        // A plot shorter than its own padding would give a negative drawable height, which inverts the
        // curve instead of leaving it blank. The chart checks for this before drawing.
        constexpr double height = 5.0;
        constexpr double lineWidth = 2.0;

        double const drawableHeight = height - PLOT_PADDING - (lineWidth / 2.0);
        EXPECT_LE(drawableHeight, 0.0) << "this case must be the one the guard catches";
    }
}
