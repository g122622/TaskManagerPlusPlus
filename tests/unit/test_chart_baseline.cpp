// Tests for the history chart's vertical scale.
//
// Full utilisation must reach the top, zero must rest on the bottom, and grid values must follow the
// same scale as plotted values. Exercise the shared production helper used by HistoryChart.
#include <gtest/gtest.h>
#include "UI/Charts/ChartScale.h"

#include <algorithm>
#include <cmath>

namespace tmpp::ui::test
{
    using ::tmpp::ui::ChartYForRatio;

    TEST(ChartBaselineTest, ZeroMapsToTheBottomEdgeLessHalfAStroke)
    {
        // The whole point of the fix: a reading of zero is drawn on the bottom of the plot, not floating
        // above it. Half the stroke is reserved so the line's lower half is not clipped by the frame.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;

        double const y = ChartYForRatio(0.0, height, lineWidth);

        EXPECT_DOUBLE_EQ(y, height - (lineWidth / 2.0)) << "zero must sit on the bottom edge";
        EXPECT_LT(y, height) << "and not below it, or the stroke would be clipped";
    }

    TEST(ChartBaselineTest, TheMaximumTouchesTheTopEdge)
    {
        // A saturated CPU must fill its chart right up to the top border.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;

        EXPECT_DOUBLE_EQ(ChartYForRatio(1.0, height, lineWidth), 0.0);
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
                double const y = ChartYForRatio(0.0, height, lineWidth);
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

        double previous = ChartYForRatio(0.0, height, lineWidth);
        for (int i = 1; i <= 10; ++i)
        {
            double const y = ChartYForRatio(static_cast<double>(i) / 10.0, height, lineWidth);
            EXPECT_LT(y, previous) << "a larger value must be drawn higher";
            previous = y;
        }
    }

    TEST(ChartBaselineTest, TheGridLinesAreEvenlySpacedAcrossThePlotAreaNotTheCanvas)
    {
        // Grid values use the same drawable height as the curve, including the bottom half stroke.
        constexpr double height = 300.0;
        constexpr double lineWidth = 2.0;
        constexpr int rows = 10;

        double const plotArea = height - (lineWidth / 2.0);
        double const expectedStep = plotArea / static_cast<double>(rows);

        for (int i = 1; i < rows; ++i)
        {
            double const y = ChartYForRatio(static_cast<double>(i) / static_cast<double>(rows), height, lineWidth);
            double const previous =
                (i == 1) ? ChartYForRatio(0.0, height, lineWidth)
                         : ChartYForRatio(static_cast<double>(i - 1) / static_cast<double>(rows), height, lineWidth);

            EXPECT_NEAR(previous - y, expectedStep, 1.0e-9) << "gap below line " << i;
        }

    }

    TEST(ChartBaselineTest, AZeroHeightPlotIsRejectedRatherThanInverted)
    {
        EXPECT_DOUBLE_EQ(ChartYForRatio(0.0, 0.0, 2.0), 0.0);
        EXPECT_DOUBLE_EQ(ChartYForRatio(1.0, 0.0, 2.0), 0.0);
        EXPECT_DOUBLE_EQ(ChartYForRatio(0.5, 1.0, 8.0), 0.0);
    }
}
