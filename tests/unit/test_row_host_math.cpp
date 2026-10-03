// Tests for the row host's virtualisation arithmetic.
//
// The host itself needs a desktop session, but the decision it makes -- which rows
// to build, and how many -- is arithmetic on the scroll offset and viewport height.
// That is what is tested here, because the failure mode is silent: getting the range
// wrong shows blank rows or leaks visuals, and neither is visible from a log.
#include <gtest/gtest.h>

#include <algorithm>

#include <cstdint>
#include <limits>

namespace tmpp::ui::test
{
    namespace
    {
        /**
         * @brief The range computation, mirroring RowHost::_updateVisibleRows.
         *
         * Duplicated rather than exposed from the class because the class needs WinRT
         * types to instantiate. If the two ever disagree, this test stops describing
         * the shipping code, so the arithmetic is kept deliberately trivial to compare
         * by eye.
         */
        struct Range
        {
            uint32_t first{0};
            uint32_t lastExclusive{0};
        };

        [[nodiscard]] Range ComputeVisibleRange(double scrollOffset,
                                                double viewportHeight,
                                                double rowHeight,
                                                uint32_t rowCount,
                                                uint32_t overscanRows)
        {
            if (rowCount == 0 || rowHeight <= 0.0)
            {
                return Range{0, 0};
            }

            // A viewport of zero (before the first layout pass) is treated as the
            // fallback the host uses, so the first rows are built immediately.
            double const effectiveViewport = (viewportHeight > 1.0) ? viewportHeight : (rowHeight * 20.0);

            auto const firstRaw = static_cast<int64_t>(scrollOffset / rowHeight);
            auto const visibleCount = static_cast<int64_t>(effectiveViewport / rowHeight) + 1;

            auto const first = static_cast<uint32_t>(
                std::clamp<int64_t>(firstRaw - static_cast<int64_t>(overscanRows), 0, rowCount));
            auto const lastExclusive = static_cast<uint32_t>(
                std::clamp<int64_t>(firstRaw + visibleCount + static_cast<int64_t>(overscanRows), 0, rowCount));

            return Range{first, lastExclusive};
        }
    }

    TEST(RowHostMathTest, EmptyListBuildsNothing)
    {
        Range const range = ComputeVisibleRange(0.0, 600.0, 28.0, 0, 4);
        EXPECT_EQ(range.first, 0u);
        EXPECT_EQ(range.lastExclusive, 0u);
    }

    TEST(RowHostMathTest, BuildsOnlyWhatFitsPlusOverscan)
    {
        // 600 px viewport at 28 px per row fits about 22 rows; with 4 rows of overscan
        // on each side the build stays near that, not near the total.
        constexpr uint32_t ROW_COUNT = 10000;
        Range const range = ComputeVisibleRange(0.0, 600.0, 28.0, ROW_COUNT, 4);

        EXPECT_EQ(range.first, 0u) << "the list starts at the top, so there is no scroll to account for";
        // 600 / 28 = 21.4 -> 22 visible, plus one partial, plus 4 overscan = 27.
        EXPECT_LE(range.lastExclusive, 32u);
        EXPECT_GT(range.lastExclusive, 20u);
        EXPECT_LT(range.lastExclusive, ROW_COUNT) << "the whole list must not be built";
    }

    TEST(RowHostMathTest, ScrollingMovesTheBuiltRange)
    {
        constexpr uint32_t ROW_COUNT = 10000;
        constexpr double ROW_HEIGHT = 28.0;

        // Scroll down 1000 rows.
        double const offset = 1000.0 * ROW_HEIGHT;
        Range const range = ComputeVisibleRange(offset, 600.0, ROW_HEIGHT, ROW_COUNT, 4);

        // The first built row is the first visible one minus overscan.
        EXPECT_EQ(range.first, 996u);
        // ...and the range is still bounded, not growing with the scroll position.
        EXPECT_LE(range.lastExclusive - range.first, 32u);
    }

    TEST(RowHostMathTest, RangeIsBoundedAtTheEndOfTheList)
    {
        // Near the bottom the range must clamp rather than run past the row count.
        constexpr uint32_t ROW_COUNT = 100;
        constexpr double ROW_HEIGHT = 28.0;
        double const offset = 99.0 * ROW_HEIGHT;

        Range const range = ComputeVisibleRange(offset, 600.0, ROW_HEIGHT, ROW_COUNT, 4);
        EXPECT_LE(range.lastExclusive, ROW_COUNT);
        EXPECT_EQ(range.lastExclusive, ROW_COUNT);
    }

    TEST(RowHostMathTest, ZeroViewportStillBuildsRows)
    {
        // Before the first layout pass the viewport reports zero. Building nothing
        // would leave the list looking empty until the user scrolled, so the host
        // substitutes a fallback height.
        Range const range = ComputeVisibleRange(0.0, 0.0, 28.0, 1000, 4);
        EXPECT_GT(range.lastExclusive, 0u) << "some rows must be built so the list is not blank";
    }

    TEST(RowHostMathTest, NegativeOffsetIsClampedToTheStart)
    {
        // A bounce or an overscroll can report a negative offset on some inputs.
        Range const range = ComputeVisibleRange(-50.0, 600.0, 28.0, 1000, 4);
        EXPECT_EQ(range.first, 0u);
    }

    TEST(RowHostMathTest, SmallListIsFullyBuilt)
    {
        // When everything fits, every row is built and overscan cannot exceed the list.
        Range const range = ComputeVisibleRange(0.0, 600.0, 28.0, 5, 4);
        EXPECT_EQ(range.first, 0u);
        EXPECT_EQ(range.lastExclusive, 5u);
    }

    TEST(RowHostMathTest, OverscanKeepsRowsReadyDuringFastScroll)
    {
        // Overscan exists so a fast scroll shows content immediately. Its effect is
        // that the built range starts before the first fully visible row.
        constexpr double ROW_HEIGHT = 28.0;
        double const offset = 100.0 * ROW_HEIGHT;

        Range const withOverscan = ComputeVisibleRange(offset, 600.0, ROW_HEIGHT, 10000, 4);
        Range const withoutOverscan = ComputeVisibleRange(offset, 600.0, ROW_HEIGHT, 10000, 0);

        EXPECT_LT(withOverscan.first, withoutOverscan.first);
        EXPECT_GT(withOverscan.lastExclusive, withoutOverscan.lastExclusive);
    }
}
