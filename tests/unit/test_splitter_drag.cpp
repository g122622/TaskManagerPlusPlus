// Tests for the splitter drag arithmetic.
//
// The drag itself needs a desktop session and a pointer, but the arithmetic that decides the width is
// pure, and it is where the visible mistakes live. The one that matters is the frame the pointer
// positions are measured in: the handle sits on the boundary it moves, so positions measured from the
// handle are measured from a frame that moves with the drag, and the boundary then follows the pointer
// at half speed instead of one to one.
//
// The rule is stated once, in UI/Theming/ResizeDrag.h, and used by the performance sidebar and the
// navigation rail; what is tested here is the rule.
#include <gtest/gtest.h>

#include <vector>

#include "UI/Theming/ResizeDrag.h"

namespace tmpp::ui::test
{
    namespace
    {
        constexpr double MIN_WIDTH = 180.0;
        constexpr double MAX_WIDTH = 520.0;

        /// A drag reported as a sequence of pointer positions, in a frame that does not move.
        [[nodiscard]] double DragTo(double startWidth, double startX, std::vector<double> const& positions)
        {
            double width = startWidth;
            for (double const x : positions)
            {
                width = DraggedWidth(startWidth, startX, x, MIN_WIDTH, MAX_WIDTH);
            }
            return width;
        }
    }

    TEST(SplitterDragTest, TheBoundaryFollowsThePointerOneForOne)
    {
        // Grabbed at 300 and dragged 60 pixels right, the boundary ends 60 pixels right -- not 30, which
        // is what a frame that moves with the handle produces.
        EXPECT_DOUBLE_EQ(DraggedWidth(300.0, 100.0, 160.0, MIN_WIDTH, MAX_WIDTH), 360.0);

        // And to the left, symmetrically.
        EXPECT_DOUBLE_EQ(DraggedWidth(300.0, 160.0, 100.0, MIN_WIDTH, MAX_WIDTH), 240.0);
    }

    TEST(SplitterDragTest, TheResultDependsOnWhereThePointerIsAndNotOnHowItGotThere)
    {
        // The property that makes the drag feel attached to the pointer: every event asks the same
        // question, so a drag sampled in one step and the same drag sampled in twenty land in the same
        // place. A width accumulated from per-event deltas would depend on the event rate instead.
        std::vector<double> const direct{260.0};

        std::vector<double> incremental;
        for (double x = 101.0; x <= 260.0; x += 1.0)
        {
            incremental.push_back(x);
        }

        EXPECT_DOUBLE_EQ(DragTo(300.0, 100.0, direct), DragTo(300.0, 100.0, incremental));
        EXPECT_DOUBLE_EQ(DragTo(300.0, 100.0, direct), 460.0);
    }

    TEST(SplitterDragTest, TheWidthIsClampedAtBothEnds)
    {
        // Neither the floor nor the ceiling may be crossed, however far the pointer is taken: a sidebar
        // dragged to nothing cannot be grabbed back, and one that swallows the page hides the charts the
        // page exists to show.
        EXPECT_DOUBLE_EQ(DraggedWidth(300.0, 100.0, -5000.0, MIN_WIDTH, MAX_WIDTH), MIN_WIDTH);
        EXPECT_DOUBLE_EQ(DraggedWidth(300.0, 100.0, 5000.0, MIN_WIDTH, MAX_WIDTH), MAX_WIDTH);

        // A width that already sits at a limit stays there rather than jumping to the other one.
        EXPECT_DOUBLE_EQ(DraggedWidth(MAX_WIDTH, 100.0, 100.0, MIN_WIDTH, MAX_WIDTH), MAX_WIDTH);
        EXPECT_DOUBLE_EQ(DraggedWidth(MIN_WIDTH, 100.0, 100.0, MIN_WIDTH, MAX_WIDTH), MIN_WIDTH);
    }

    TEST(SplitterDragTest, AFrameThatMovesWithTheHandleUnderTracksTheDrag)
    {
        // Why the positions must come from a frame that does not move. The handle is placed on the
        // boundary, so as a drag widens the pane the handle moves with it; a position measured from the
        // handle therefore loses the movement already applied from the next reading. Simulating exactly
        // that shows the boundary lagging: the pointer travels 300 pixels and the boundary moves 150.
        double const startWidth = 300.0;
        double const startX = 100.0;

        double width = startWidth;
        double handleOrigin = 0.0;

        for (double pointer = 110.0; pointer <= 400.0; pointer += 10.0)
        {
            // The same pointer travel, read in the handle's moving frame.
            double const measured = pointer - handleOrigin;
            double const next = DraggedWidth(startWidth, startX, measured, MIN_WIDTH, MAX_WIDTH);

            handleOrigin += next - width;
            width = next;
        }

        EXPECT_DOUBLE_EQ(width - startWidth, 150.0)
            << "half the pointer's 300 pixel travel: this is the defect the frame requirement prevents";
    }
}
