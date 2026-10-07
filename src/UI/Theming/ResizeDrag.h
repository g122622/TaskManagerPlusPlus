// The arithmetic of a splitter drag.
//
// Kept out of the control that performs the drag so it can be tested: the arithmetic is where the
// mistakes are, and the same rule serves every boundary in the application -- the process list's
// columns, the navigation rail and the performance sidebar all move a boundary by the pointer's
// movement from where the drag began.
#pragma once

#include <algorithm>

namespace tmpp::ui
{
    /**
     * @brief The width a splitter drag asks for.
     *
     * Measuring the pointer's movement rather than its absolute position is what keeps the boundary
     * under the cursor wherever it was grabbed, instead of jumping so that the boundary lands on the
     * cursor.
     *
     * @param startWidth Width of the pane when the drag began.
     * @param startX Pointer position when the drag began.
     * @param currentX Pointer position now.
     * @param minWidth Smallest width the pane may be dragged to.
     * @param maxWidth Largest width the pane may be dragged to.
     *
     * Both positions must come from a frame that does not move while the drag runs -- the window's, for
     * instance. Measured from the handle itself, which sits on the boundary and therefore moves as the
     * width changes, the handle's own movement cancels the pointer's: the boundary then tracks at half
     * speed, and a drag on a boundary that has not moved yet starts from the wrong width.
     */
    [[nodiscard]] constexpr double DraggedWidth(double startWidth,
                                               double startX,
                                               double currentX,
                                               double minWidth,
                                               double maxWidth) noexcept
    {
        return std::clamp(startWidth + (currentX - startX), minWidth, maxWidth);
    }
}
