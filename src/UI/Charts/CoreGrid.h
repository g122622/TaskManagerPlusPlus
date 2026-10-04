// The per-logical-processor chart grid.
//
// Windows 11 Task Manager draws one small chart per logical processor, arranged in a
// grid whose column count adapts to the width available. Reproducing that needs two
// things the rest of the UI does not: a column count derived from the container width,
// and a bounded number of charts so a 64-core machine does not create 64 control trees
// on every layout pass.
//
// The column count is chosen to keep each cell close to the original's aspect ratio
// rather than to fill the width exactly. A machine with a prime core count would
// otherwise end up with a ragged last row of very different cell sizes, which reads as
// a layout bug rather than as a design.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace tmpp::ui
{
    /**
     * @brief A responsive grid of one small chart per logical processor.
     */
    class CoreGrid
    {
    public:
        /**
         * @brief Fills one cell.
         *
         * Called when a chart's series changes, so the host does not own the data
         * source and the page keeps deciding what each core's chart shows.
         */
        using CellBinder = std::function<void(uint32_t coreIndex, std::vector<double> const& series)>;

        CoreGrid();
        ~CoreGrid();

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /**
         * @brief Sets how many cores to draw.
         *
         * Rebuilds the grid only when the count changes, since the count is fixed for
         * the lifetime of a run.
         */
        void SetCoreCount(uint32_t coreCount);

        /**
         * @brief Pushes new series into the existing charts. Does not rebuild.
         *
         * @param perCoreSeries One series per logical processor.
         * @param windowSamples Samples representing the full time window, so each cell
         *        anchors its data to the right rather than stretching it across the width.
         */
        void UpdateSeries(std::vector<std::vector<double>> const& perCoreSeries, size_t windowSamples);

        /// Colour applied to every cell.
        void SetColor(winrt::Windows::UI::Color color);

        /// Applies a stroke width to every cell, driven by the user's setting.
        void SetLineWidth(double width);
        /// Number of charts currently built. Exposed for diagnostics and tests.
        [[nodiscard]] size_t CellCount() const noexcept { return m_cells.size(); }

        /// Column count for the current width. Exposed for diagnostics and tests.
        [[nodiscard]] uint32_t ColumnCount() const noexcept { return m_columns; }

        /**
         * @brief Computes the column count for a given width.
         *
         * Static and pure so the choice is testable without a visual tree; the
         * arithmetic is the part that can be wrong.
         *
         * @param availableWidth Width to lay cells out in, in effective pixels.
         * @param coreCount Number of cells to place.
         */
        [[nodiscard]] static uint32_t ColumnsForWidth(double availableWidth, uint32_t coreCount);

        /**
         * @brief Computes the column count that keeps cells close to the target aspect.
         *
         * The height matters as much as the width, and ignoring it was a real defect: a
         * 556x400 area with 28 cores gave three columns, ten rows, and cells 178 wide by
         * 36 tall. A cell that wide and that short leaves the chart canvas below its
         * minimum drawing height, so every chart silently rendered blank.
         *
         * The row count is therefore derived from the height first, and the column count
         * follows from it. That guarantees cells are at least the target height whenever
         * the area can afford the rows, which the width-only calculation could not.
         *
         * @param availableWidth Width available, in effective pixels.
         * @param availableHeight Height available, in effective pixels.
         * @param coreCount Number of cells to place.
         */
        [[nodiscard]] static uint32_t ColumnsForSize(double availableWidth,
                                                     double availableHeight,
                                                     uint32_t coreCount);

        /// Smallest row height that still lets a chart draw. Exposed so the CPU page can
        /// size its container to match rather than guessing.
        [[nodiscard]] static double MinimumRowHeight() noexcept;

        /// Preferred cell width that follows from the target aspect. Exposed for the
        /// page's sizing and for tests.
        [[nodiscard]] static double TargetCellWidth() noexcept;

    private:
        /// Rebuilds the grid rows and columns for the current width and core count.
        void _rebuildLayout();

        /// Recomputes the column count from the current width and rebuilds if it
        /// changed. This is what makes the grid respond to a window resize.
        void _onSizeChanged();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};

        struct CoreCell;
        std::vector<std::unique_ptr<CoreCell>> m_cells;

        uint32_t m_coreCount{0};
        uint32_t m_columns{0};
        double m_lastWidth{0.0};

        /**
         * @brief The style a cell is given as it is created.
         *
         * Held here rather than only pushed into the cells that exist, because the grid creates cells long
         * after the style is set: the arrangement is rebuilt whenever the count or the width changes, and a
         * cell that appears in a later rebuild would otherwise take the chart's own default. That is what
         * left a resized window -- or a settings change made before the grid had settled -- drawing some
         * cells at the configured width and others at the default.
         */
        double m_lineWidth{1.0};
        winrt::Windows::UI::Color m_color{winrt::Windows::UI::Colors::DodgerBlue()};
    };
}
