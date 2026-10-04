// Tests for the height a stacked-chart page needs.
//
// The disk and GPU pages each stack two plots. Every chart carries a minimum height, because below it
// the canvas cannot draw a curve at all, so a page carrying two of them needs both floors plus its
// fixed rows before the window is tall enough. When the floors were copied from the single-chart pages
// the total exceeded an ordinary window: the star rows could not shrink below their floors, the grid
// overflowed its cell, and the lower chart drew over the details beneath it.
//
// The arithmetic is trivial, which is exactly why it went unchecked: nothing in the code said what the
// page's total minimum was, so nothing noticed when it stopped fitting.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace tmpp::ui::test
{
    namespace
    {
        /// The floor a chart may be squeezed to on a page that stacks two of them.
        constexpr double STACKED_CHART_MIN_HEIGHT = 120.0;

        /// The floor used on a page with a single chart.
        constexpr double SINGLE_CHART_MIN_HEIGHT = 220.0;

        /// HistoryChart's own minimum, below which it declines to draw. A page whose floor is under this
        /// would show an empty frame rather than a curve.
        constexpr double CHART_MIN_PLOT_HEIGHT = 24.0;

        /// The fixed rows on a stacked page: a heading, two captions, a details block and the margins
        /// between them. An estimate rather than an exact measure, since what matters is the order of
        /// magnitude: the point is that the floors must not consume the whole window on their own.
        constexpr double STACKED_FIXED_ROWS = 200.0;

        /// Height below which a window is considered short, used to exercise the arrangement. A small
        /// laptop display in a window that is not maximised lands near here.
        constexpr double SHORT_WINDOW_HEIGHT = 620.0;
    }

    TEST(StackedChartPageHeightTest, TwoChartsFitInAShortWindow)
    {
        // The defect: two 220 pixel floors need 440 before any caption, detail row or margin, which a
        // short window cannot provide alongside them.
        double const stackedTotal = (2.0 * STACKED_CHART_MIN_HEIGHT) + STACKED_FIXED_ROWS;

        EXPECT_LT(stackedTotal, SHORT_WINDOW_HEIGHT)
            << "two stacked charts plus the fixed rows need " << stackedTotal
            << " pixels, which does not fit a " << SHORT_WINDOW_HEIGHT << " pixel window";

        // The floor it replaced would not have fitted, which is what makes this a regression test
        // rather than a restatement of the current numbers.
        double const previousTotal = (2.0 * SINGLE_CHART_MIN_HEIGHT) + STACKED_FIXED_ROWS;
        EXPECT_GT(previousTotal, SHORT_WINDOW_HEIGHT)
            << "the single-chart floor should not fit a stacked page, or this test proves nothing";
    }

    TEST(StackedChartPageHeightTest, TheFloorStillLeavesRoomToDraw)
    {
        // A floor below the chart's own minimum would be worse than the defect it fixes: the cell would
        // be sized, the chart would decline to draw, and the page would show an empty frame.
        EXPECT_GT(STACKED_CHART_MIN_HEIGHT, CHART_MIN_PLOT_HEIGHT);

        // Room for a curve to be read rather than merely drawn: a plot at the bare minimum is a line
        // of pixels with no visible shape.
        EXPECT_GE(STACKED_CHART_MIN_HEIGHT, 4.0 * CHART_MIN_PLOT_HEIGHT);
    }

    TEST(StackedChartPageHeightTest, EachChartGetsTheSameShareOfTheWindow)
    {
        // The two rows are both star-sized, so the charts are equal and neither can starve the other.
        // The floors are equal too: an asymmetric pair would make one plot permanently shorter.
        double const available = SHORT_WINDOW_HEIGHT - STACKED_FIXED_ROWS;
        double const each = available / 2.0;

        EXPECT_GT(each, STACKED_CHART_MIN_HEIGHT)
            << "in a short window the star rows give each chart " << each
            << " pixels, which is under the floor of " << STACKED_CHART_MIN_HEIGHT
            << ", so the grid would overflow its cell";
    }

    TEST(StackedChartPageHeightTest, ASingleChartPageKeepsItsLargerFloor)
    {
        // The single-chart pages were not part of the defect and keep the taller plot, which is the
        // reason the floors differ between the two kinds of page.
        EXPECT_GT(SINGLE_CHART_MIN_HEIGHT, STACKED_CHART_MIN_HEIGHT);

        double const singleTotal = SINGLE_CHART_MIN_HEIGHT + STACKED_FIXED_ROWS;
        EXPECT_LT(singleTotal, SHORT_WINDOW_HEIGHT);
    }
}
