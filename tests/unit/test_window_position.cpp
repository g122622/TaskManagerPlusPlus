// Tests for the saved-window-position guard.
//
// A remembered position can refer to a monitor that is no longer attached. Restoring it
// blindly puts the window where the user cannot reach it, and that presents as the
// application failing to start -- there is no window, no error, and no clue. This was a real
// case: a position saved on a 1795 by 1132 desktop was restored on a 1024 by 768 one, placing
// the window entirely below the visible area.
//
// The predicate is pure arithmetic over virtual-desktop bounds, so it is tested directly
// rather than through the window.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

namespace tmpp::app::test
{
    namespace
    {
        /// Mirrors MainWindow::_isPositionVisible.
        ///
        /// Duplicated because the real one calls GetSystemMetrics for the desktop bounds,
        /// which cannot be varied in a test. Parameterising the bounds here is what makes the
        /// cases below expressible at all.
        [[nodiscard]] bool IsPositionVisible(int32_t x,
                                            int32_t y,
                                            int32_t width,
                                            int32_t height,
                                            int32_t virtualLeft,
                                            int32_t virtualTop,
                                            int32_t virtualWidth,
                                            int32_t virtualHeight)
        {
            if (virtualWidth <= 0 || virtualHeight <= 0)
            {
                return false;
            }

            int32_t const virtualRight = virtualLeft + virtualWidth;
            int32_t const virtualBottom = virtualTop + virtualHeight;

            constexpr int32_t MIN_VISIBLE = 64;
            int32_t const visibleWidth = std::min(x + width, virtualRight) - std::max(x, virtualLeft);
            int32_t const visibleHeight = std::min(y + height, virtualBottom) - std::max(y, virtualTop);

            return visibleWidth >= MIN_VISIBLE && visibleHeight >= MIN_VISIBLE;
        }

        /// The desktop the failing case was restored onto.
        constexpr int32_t PRIMARY_W = 1024;
        constexpr int32_t PRIMARY_H = 768;

        [[nodiscard]] bool OnPrimary(int32_t x, int32_t y, int32_t width, int32_t height)
        {
            return IsPositionVisible(x, y, width, height, 0, 0, PRIMARY_W, PRIMARY_H);
        }
    }

    TEST(WindowPositionTest, TheReportedOffScreenPositionIsRejected)
    {
        // The exact saved values: a window at (481, 908) on a 1024 by 768 desktop. Its top
        // edge is below the bottom of every display, so none of it would be reachable.
        EXPECT_FALSE(OnPrimary(481, 908, 1795, 1132));
    }

    TEST(WindowPositionTest, APositionFullyOnScreenIsAccepted)
    {
        EXPECT_TRUE(OnPrimary(100, 100, 800, 600));
    }

    TEST(WindowPositionTest, TheDefaultTopLeftCornerIsAccepted)
    {
        EXPECT_TRUE(OnPrimary(0, 0, 800, 600));
    }

    TEST(WindowPositionTest, AWindowLargerThanTheScreenIsAccepted)
    {
        // Oversized is not the same as off-screen: a window bigger than the display still has
        // its top-left reachable, and clamping its size is a separate concern.
        EXPECT_TRUE(OnPrimary(0, 0, 3000, 2000));
    }

    TEST(WindowPositionTest, AWindowHangingOffTheRightEdgeIsAccepted)
    {
        // Half the window is past the right edge but the left half is usable.
        EXPECT_TRUE(OnPrimary(800, 100, 800, 600));
    }

    TEST(WindowPositionTest, ASliverOnScreenIsRejected)
    {
        // Only ten pixels of the window overlap the desktop. It is technically visible but
        // cannot be grabbed or dragged back, so it is treated as unreachable.
        EXPECT_FALSE(OnPrimary(1014, 100, 800, 600));

        // Just past the threshold is accepted.
        EXPECT_TRUE(OnPrimary(960, 100, 800, 600));
    }

    TEST(WindowPositionTest, AWindowAlmostEntirelyBelowTheScreenIsRejected)
    {
        // y = 700 on a 768 pixel display leaves 68 pixels visible, which is above the
        // threshold and is therefore accepted: the title bar can still be grabbed. The case
        // below pushes the window far enough down that too little remains.
        EXPECT_FALSE(OnPrimary(100, 750, 800, 600));
        EXPECT_FALSE(OnPrimary(100, 900, 800, 600));
    }

    TEST(WindowPositionTest, AWindowWithOnlyItsTopStripVisibleIsRejected)
    {
        // 64 pixels is the threshold: a 63 pixel strip cannot be reliably grabbed, and 64
        // can. Both sides of the boundary are pinned so a change to it is deliberate.
        EXPECT_FALSE(OnPrimary(100, 705, 800, 600)) << "63 pixels visible";
        EXPECT_TRUE(OnPrimary(100, 704, 800, 600)) << "64 pixels visible";
    }

    TEST(WindowPositionTest, AWindowAboveOrLeftOfTheScreenIsRejected)
    {
        // Negative coordinates happen when a display to the left or above is removed.
        EXPECT_FALSE(OnPrimary(-900, 100, 800, 600));
        EXPECT_FALSE(OnPrimary(100, -700, 800, 600));
    }

    TEST(WindowPositionTest, ASecondMonitorToTheRightIsAccepted)
    {
        // A virtual desktop of two 1024-wide displays side by side. A window on the second
        // one is valid and must not be rejected for being outside the primary bounds.
        EXPECT_TRUE(IsPositionVisible(1200, 100, 800, 600, 0, 0, 2048, 768));
    }

    TEST(WindowPositionTest, ASecondMonitorToTheLeftWithNegativeCoordinatesIsAccepted)
    {
        // Displays arranged to the left give the virtual desktop a negative origin.
        EXPECT_TRUE(IsPositionVisible(-900, 100, 800, 600, -1024, 0, 2048, 768));
    }

    TEST(WindowPositionTest, AnEmptyDesktopBoundsIsRejected)
    {
        // The metrics should never report zero, but refusing to move the window is the safe
        // response: it stays at the platform default, which is always visible.
        EXPECT_FALSE(IsPositionVisible(100, 100, 800, 600, 0, 0, 0, 0));
        EXPECT_FALSE(IsPositionVisible(100, 100, 800, 600, 0, 0, -1, -1));
    }
}
