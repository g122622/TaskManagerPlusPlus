// Tests for the core grid's column arithmetic.
//
// The grid itself needs a desktop session, but choosing how many columns a width and height
// afford is pure arithmetic, and it is where mistakes hide: a wrong column count produces a
// grid of unusably narrow cells or one that is far from the original's proportions, and
// neither is visible from a log.
//
// Two real defects came from this arithmetic, and both are pinned below:
//
//   * cells far shorter than a chart can draw in, because only the width was considered;
//   * a transient layout pass producing sixteen columns of about seven pixels, because a
//     fallback ignored the width constraint the rest of the function respected.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tmpp::ui::test
{
    namespace
    {
        // Mirrors CoreGrid. Kept in step with the implementation by hand; the arithmetic is
        // deliberately trivial so the two can be compared by eye.
        constexpr double TARGET_CELL_ASPECT = 1.15;
        constexpr double MIN_CELL_HEIGHT = 40.0;
        constexpr double MIN_CELL_WIDTH = 56.0;
        constexpr uint32_t MIN_COLUMNS = 1;
        constexpr uint32_t MAX_COLUMNS = 16;
        constexpr double CELL_GAP = 4.0;

        [[nodiscard]] uint32_t ColumnsForWidth(double availableWidth, uint32_t coreCount)
        {
            if (coreCount == 0 || availableWidth <= 0.0)
            {
                return MIN_COLUMNS;
            }

            auto const fits =
                static_cast<uint32_t>(std::floor((availableWidth + CELL_GAP) / (MIN_CELL_WIDTH + CELL_GAP)));
            uint32_t columns = std::clamp(fits, MIN_COLUMNS, MAX_COLUMNS);
            return std::max(1u, std::min(columns, coreCount));
        }

        [[nodiscard]] uint32_t ColumnsForSize(double availableWidth, double availableHeight, uint32_t coreCount)
        {
            if (coreCount == 0)
            {
                return MIN_COLUMNS;
            }
            if (availableWidth <= 0.0 || availableHeight <= 0.0)
            {
                return std::min(coreCount, 4u);
            }

            uint32_t bestColumns = 1;
            double bestError = 1.0e30;
            uint32_t const maxColumns = std::min(coreCount, MAX_COLUMNS);

            for (uint32_t columns = 1; columns <= maxColumns; ++columns)
            {
                uint32_t const rows = (coreCount + columns - 1) / columns;
                double const cellWidth = (availableWidth - (CELL_GAP * (columns - 1))) / columns;
                double const cellHeight = (availableHeight - (CELL_GAP * (rows - 1))) / rows;

                if (cellHeight < MIN_CELL_HEIGHT || cellWidth < MIN_CELL_WIDTH)
                {
                    continue;
                }

                double const error = std::abs((cellWidth / cellHeight) - TARGET_CELL_ASPECT);
                if (error < bestError)
                {
                    bestError = error;
                    bestColumns = columns;
                }
            }

            if (bestError >= 1.0e30)
            {
                return std::max(1u, ColumnsForWidth(availableWidth, coreCount));
            }
            return bestColumns;
        }
    }

    // ------------------------------------------------------------------------
    // The arrangement the original uses
    // ------------------------------------------------------------------------

    TEST(CoreGridSizeTest, MatchesTheOriginalForTwentyEightCores)
    {
        // Windows 11 Task Manager lays 28 logical processors out as six columns by five rows
        // at a typical window size. This is the arrangement the grid exists to reproduce.
        constexpr uint32_t CORES = 28;
        uint32_t const columns = ColumnsForSize(592.0, 375.0, CORES);
        uint32_t const rows = (CORES + columns - 1) / columns;

        EXPECT_EQ(columns, 6u) << "the measured area must produce the original's six columns";
        EXPECT_EQ(rows, 5u);
    }

    TEST(CoreGridSizeTest, EveryCellCanDrawAtThatSize)
    {
        // The arrangement is only valid if the cells are actually drawable.
        constexpr uint32_t CORES = 28;
        constexpr double WIDTH = 592.0;
        constexpr double HEIGHT = 375.0;

        uint32_t const columns = ColumnsForSize(WIDTH, HEIGHT, CORES);
        uint32_t const rows = (CORES + columns - 1) / columns;
        double const cellWidth = (WIDTH - (CELL_GAP * (columns - 1))) / columns;
        double const cellHeight = (HEIGHT - (CELL_GAP * (rows - 1))) / rows;

        EXPECT_GE(cellHeight, MIN_CELL_HEIGHT);
        EXPECT_GE(cellWidth, MIN_CELL_WIDTH);
    }

    // ------------------------------------------------------------------------
    // Defect 1: height ignored, cells too short to draw
    // ------------------------------------------------------------------------

    TEST(CoreGridSizeTest, WideShortAreasDoNotProduceUnslakableCells)
    {
        // A wide, short area is where ignoring the height went wrong: the width alone
        // suggests many columns, which forces many rows, which leaves no height per cell.
        constexpr uint32_t CORES = 28;
        constexpr double WIDTH = 1200.0;
        constexpr double HEIGHT = 300.0;

        uint32_t const columns = ColumnsForSize(WIDTH, HEIGHT, CORES);
        uint32_t const rows = (CORES + columns - 1) / columns;
        double const cellHeight = (HEIGHT - (CELL_GAP * (rows - 1))) / rows;

        EXPECT_GE(cellHeight, MIN_CELL_HEIGHT)
            << "columns=" << columns << " rows=" << rows << " cellHeight=" << cellHeight;
    }

    TEST(CoreGridSizeTest, TallerAreasAllowFewerColumns)
    {
        // More height means more rows are affordable, so fewer columns are needed to reach
        // the target aspect. This is the direction the height constraint pulls in.
        uint32_t const shortArea = ColumnsForSize(700.0, 160.0, 28);
        uint32_t const tallArea = ColumnsForSize(700.0, 900.0, 28);

        EXPECT_GE(shortArea, tallArea);
    }

    // ------------------------------------------------------------------------
    // Defect 2: the fallback ignored the width constraint
    // ------------------------------------------------------------------------

    TEST(CoreGridSizeTest, TinyAreasDoNotProduceHairThinColumns)
    {
        // A transient layout pass reported a 184 by 160 area. The previous fallback used
        // every allowed column and produced cells about seven pixels wide, which is worse
        // than showing fewer, wider cells. The fallback must respect the width rule.
        constexpr uint32_t CORES = 28;
        uint32_t const columns = ColumnsForSize(184.0, 160.0, CORES);

        EXPECT_LE(columns, ColumnsForWidth(184.0, CORES))
            << "the fallback must not exceed what the width allows";
        EXPECT_LE(columns, 4u) << "a 184 pixel area holds three or four cells, not sixteen";
    }

    TEST(CoreGridSizeTest, FallbackStillProducesAtLeastOneColumn)
    {
        // However small the area, the grid must produce an arrangement rather than zero.
        EXPECT_GE(ColumnsForSize(1.0, 1.0, 28), 1u);
        EXPECT_GE(ColumnsForSize(10.0, 10.0, 28), 1u);
    }

    // ------------------------------------------------------------------------
    // The purely horizontal constraint
    // ------------------------------------------------------------------------

    TEST(CoreGridColumnsTest, NeverExceedsWhatTheWidthHolds)
    {
        // At the minimum cell width, a 100 pixel area holds one cell, not several.
        EXPECT_EQ(ColumnsForWidth(100.0, 28), 1u);
        EXPECT_EQ(ColumnsForWidth(200.0, 28), 3u) << "three 56 pixel cells plus gaps fit in 200";
    }

    TEST(CoreGridColumnsTest, NeverMoreColumnsThanCores)
    {
        EXPECT_LE(ColumnsForWidth(4000.0, 3), 3u);
        EXPECT_EQ(ColumnsForWidth(4000.0, 1), 1u);
    }

    TEST(CoreGridColumnsTest, ZeroWidthOrZeroCoresYieldsTheMinimum)
    {
        EXPECT_EQ(ColumnsForWidth(0.0, 28), MIN_COLUMNS);
        EXPECT_EQ(ColumnsForSize(0.0, 0.0, 0), MIN_COLUMNS);
        EXPECT_EQ(ColumnsForWidth(500.0, 0), MIN_COLUMNS);
    }

    TEST(CoreGridColumnsTest, ZeroHeightFallsBackToAProvisionalArrangement)
    {
        // Before the first layout pass the size is unknown; a provisional answer is returned
        // and the size-changed handler rebuilds once the real size arrives.
        EXPECT_EQ(ColumnsForSize(592.0, 0.0, 28), 4u);
    }
}
