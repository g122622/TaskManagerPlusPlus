#include "UI/WinRTUI.h"

#include "UI/Charts/CoreGrid.h"

#include "UI/Diagnostics.h"
#include "UI/Charts/HistoryChart.h"
#include "UI/Theming/Controls.h"
#include "UI/Theming/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::Border;
using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp::ui
{
    namespace
    {
        /// Preferred cell shape, as width divided by height.
        ///
        /// Measured from the original: 28 logical processors occupy six columns by five
        /// rows in an area about 510 by 380, giving cells of roughly 85 by 76 and an aspect
        /// near 1.12. The arrangement is chosen to land close to this rather than to fill
        /// the width, because a grid of very wide short cells reads as a layout fault.
        constexpr double TARGET_CELL_ASPECT = 1.15;

        /// Smallest cell height that still draws a chart.
        ///
        /// Must exceed HistoryChart's own MIN_PLOT_HEIGHT of 24, plus the frame's border
        /// and margin. A cell shorter than this cannot show a curve at all.
        constexpr double MIN_CELL_HEIGHT = 40.0;

        /// Smallest usable cell width. Narrower than this and a burst of activity is not
        /// legible.
        constexpr double MIN_CELL_WIDTH = 56.0;

        /// Bounds on the column count, so a degenerate size still produces something sane.
        constexpr uint32_t MIN_COLUMNS = 1;
        constexpr uint32_t MAX_COLUMNS = 16;


        /// Gap between cells.
        constexpr double CELL_GAP = 4.0;
    }

    /**
     * @brief One core's chart: a bordered cell holding a headerless HistoryChart.
     */
    struct CoreGrid::CoreCell
    {
        Border frame{nullptr};
        std::unique_ptr<HistoryChart> chart;
    };

    CoreGrid::CoreGrid()
    {
        m_root = Grid();
        m_root.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        m_root.VerticalAlignment(VerticalAlignment::Stretch);

        // The grid rearranges itself when the space it has changes, which is what makes
        // the column count adapt to the window rather than being fixed at construction.
        m_root.SizeChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                  winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const&) { _onSizeChanged(); });
    }

    CoreGrid::~CoreGrid() = default;

    uint32_t CoreGrid::ColumnsForWidth(double availableWidth, uint32_t coreCount)
    {
        if (coreCount == 0 || availableWidth <= 0.0)
        {
            return MIN_COLUMNS;
        }

        // The most columns a cell can be packed into while every cell stays at least
        // MIN_CELL_WIDTH wide. This is the purely horizontal constraint: it says nothing
        // about height, which is what ColumnsForSize adds.
        auto const fits =
            static_cast<uint32_t>(std::floor((availableWidth + CELL_GAP) / (MIN_CELL_WIDTH + CELL_GAP)));

        uint32_t columns = (std::clamp)(fits, MIN_COLUMNS, MAX_COLUMNS);

        // Never use more columns than there are cells: three cores in an eight column grid
        // would leave most of the row empty and make each chart needlessly narrow.
        columns = (std::min)(columns, coreCount);

        return (std::max)(1u, columns);
    }

    double CoreGrid::MinimumRowHeight() noexcept
    {
        return MIN_CELL_HEIGHT;
    }

    double CoreGrid::TargetCellWidth() noexcept
    {
        return TARGET_CELL_ASPECT * MIN_CELL_HEIGHT;
    }

    uint32_t CoreGrid::ColumnsForSize(double availableWidth, double availableHeight, uint32_t coreCount)
    {
        if (coreCount == 0)
        {
            return MIN_COLUMNS;
        }
        if (availableWidth <= 0.0 || availableHeight <= 0.0)
        {
            // Before the first layout pass neither dimension is known. A provisional
            // arrangement is returned and the size-changed handler rebuilds once the real
            // size arrives.
            return (std::min)(coreCount, 4u);
        }

        // Choose the column count whose cells come closest to the target aspect.
        //
        // Trying every candidate is deliberate. A closed-form answer exists for filling an
        // area, but the goal here is not to fill it: it is to make the cells a particular
        // shape, and the aspect of a candidate depends on both the column count and the
        // resulting row count through integer division, which is not monotonic. For 28
        // cores a direct calculation lands on five or seven columns where six is the fit,
        // so the candidates are measured rather than derived.
        uint32_t bestColumns = 1;
        double bestError = 1.0e30;

        uint32_t const maxColumns = (std::min)(coreCount, MAX_COLUMNS);

        for (uint32_t columns = 1; columns <= maxColumns; ++columns)
        {
            uint32_t const rows = (coreCount + columns - 1) / columns;

            // Cell size after the gaps between cells are taken out of the area.
            double const cellWidth = (availableWidth - (CELL_GAP * (columns - 1))) / columns;
            double const cellHeight = (availableHeight - (CELL_GAP * (rows - 1))) / rows;

            // A cell that cannot draw is not a candidate, however good its aspect.
            if (cellHeight < MIN_CELL_HEIGHT || cellWidth < MIN_CELL_WIDTH)
            {
                continue;
            }

            double const aspect = cellWidth / cellHeight;
            double const error = std::abs(aspect - TARGET_CELL_ASPECT);

            // Strictly better only, so ties keep the smaller column count and the grid
            // stays closer to square rather than stretching into a single wide row.
            if (error < bestError)
            {
                bestError = error;
                bestColumns = columns;
            }
        }

        // If no candidate could draw, the area is too small for even one cell at the minimum
        // size. This happens during the first layout pass, before the parent has settled, and
        // the grid is rebuilt moments later. The answer is still bounded by the width: using
        // every allowed column in a 184 pixel area produced cells about seven pixels wide,
        // which is a worse arrangement than simply showing fewer, wider cells.
        if (bestError >= 1.0e30)
        {
            return (std::max)(1u, ColumnsForWidth(availableWidth, coreCount));
        }

        return bestColumns;
    }

    void CoreGrid::SetCoreCount(uint32_t coreCount)
    {
        if (coreCount == m_coreCount)
        {
            return;
        }

        m_coreCount = coreCount;

        // Force a rebuild: the cell count changed, so the arrangement must be recomputed
        // regardless of whether the size did.
        m_lastWidth = 0.0;
        _rebuildLayout();
    }

    void CoreGrid::SetColor(winrt::Windows::UI::Color color)
    {
        for (auto& cell : m_cells)
        {
            if (cell->chart != nullptr)
            {
                cell->chart->SetLineColor(color);
            }
        }
    }

    void CoreGrid::_onSizeChanged()
    {
        double const width = m_root.ActualWidth();
        double const height = m_root.ActualHeight();
        if (width <= 1.0)
        {
            return;
        }

        // Rebuild only when the resulting column count would differ. Comparing sizes
        // directly would rebuild on every pixel of a resize drag.
        uint32_t const newColumns = ColumnsForSize(width, height, m_coreCount);
        if (newColumns == m_columns && std::abs(width - m_lastWidth) < 1.0)
        {
            return;
        }

        m_lastWidth = width;
        _rebuildLayout();
    }

    void CoreGrid::_rebuildLayout()
    {
        // Both dimensions are used: the height decides how many rows are affordable, and
        // the column count follows. Sizing on width alone produced cells far too short for
        // a chart to draw in.
        double const width = (m_root.ActualWidth() > 1.0) ? m_root.ActualWidth() : TargetCellWidth() * 4.0;
        double const height = (m_root.ActualHeight() > 1.0) ? m_root.ActualHeight() : MIN_CELL_HEIGHT * 4.0;

        m_columns = ColumnsForSize(width, height, m_coreCount);
        if (m_coreCount == 0)
        {
            m_cells.clear();
            m_root.Children().Clear();
            m_root.ColumnDefinitions().Clear();
            m_root.RowDefinitions().Clear();
            return;
        }

        uint32_t const rows = (m_coreCount + m_columns - 1) / m_columns;

        // Reuse the existing charts where possible. Destroying and recreating them on
        // every resize would discard the series and blank the grid mid-drag.
        std::vector<std::unique_ptr<CoreCell>> reused = std::move(m_cells);
        m_cells.clear();
        m_cells.reserve(m_coreCount);

        m_root.Children().Clear();
        m_root.ColumnDefinitions().Clear();
        m_root.RowDefinitions().Clear();

        // Reported when the arrangement changes rather than on every update: the useful
        // signal is which grid a given size produced, and a per-sample report would bury it.
        if (diagnostics::Enabled())
        {
            diagnostics::Report("coregrid: cores=" + std::to_string(m_coreCount) + " columns=" +
                                std::to_string(m_columns) + " rows=" + std::to_string(rows) + " area=" +
                                std::to_string(static_cast<int>(width)) + "x" +
                                std::to_string(static_cast<int>(height)));
        }

        for (uint32_t column = 0; column < m_columns; ++column)
        {
            ColumnDefinition definition;
            // Star columns divide the width evenly, so no pixel arithmetic is needed and
            // the cells stay correct at any window size.
            definition.Width(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            m_root.ColumnDefinitions().Append(definition);
        }

        for (uint32_t row = 0; row < rows; ++row)
        {
            RowDefinition definition;
            // Star rows share the available height, but each carries a floor so the cells
            // stay tall enough to draw in. Without the floor a 28-core grid in a short
            // container produced rows about 14 pixels tall and every chart drew nothing,
            // which looked exactly like missing data.
            definition.MinHeight(MIN_CELL_HEIGHT);
            definition.Height(GridLengthHelper::FromValueAndType(1.0, GridUnitType::Star));
            m_root.RowDefinitions().Append(definition);
        }

        for (uint32_t index = 0; index < m_coreCount; ++index)
        {
            std::unique_ptr<CoreCell> cell;

            if (index < reused.size() && reused[index] != nullptr && reused[index]->chart != nullptr)
            {
                cell = std::move(reused[index]);
            }
            else
            {
                cell = std::make_unique<CoreCell>();

                // The cell is just a positioned wrapper: the outline belongs to the chart inside
                // it, which builds its own frame through the shared factory. Drawing a second
                // border here would double the line and, as before, in a different colour.
                cell->frame = Border();
                cell->frame.Margin(ThicknessHelper::FromUniformLength(CELL_GAP / 2.0));
                cell->frame.CornerRadius(winrt::Microsoft::UI::Xaml::CornerRadiusHelper::FromUniformRadius(
                    metrics::CHART_CORNER_RADIUS));

                // Each cell is a full HistoryChart with its header hidden. Reusing the component
                // rather than writing a second chart keeps one implementation of the drawing
                // maths, which is the part that has to be right.
                cell->chart = std::make_unique<HistoryChart>(L"", winrt::Windows::UI::Colors::DodgerBlue(), 100.0);
                cell->chart->SetHeaderVisible(false);

                cell->frame.Child(cell->chart->Root());
            }

            uint32_t const row = index / m_columns;
            uint32_t const column = index % m_columns;

            Grid::SetRow(cell->frame, static_cast<int32_t>(row));
            Grid::SetColumn(cell->frame, static_cast<int32_t>(column));
            m_root.Children().Append(cell->frame);

            m_cells.push_back(std::move(cell));
        }
    }

    void CoreGrid::UpdateSeries(std::vector<std::vector<double>> const& perCoreSeries, size_t windowSamples)
    {
        for (size_t i = 0; i < m_cells.size(); ++i)
        {
            if (m_cells[i] == nullptr || m_cells[i]->chart == nullptr)
            {
                continue;
            }

            // The window travels with every series, so no cell can be left fitting its data
            // to the full width. Omitting it is what drew a partly filled grid as though each
            // core had a complete history.
            ChartSeries series;
            series.windowSamples = windowSamples;
            if (i < perCoreSeries.size())
            {
                series.values = perCoreSeries[i];
            }
            // A core with no series leaves its cell empty rather than drawing a flat line,
            // which would claim a measurement that does not exist.

            m_cells[i]->chart->SetSeries(series);
        }
    }
}
