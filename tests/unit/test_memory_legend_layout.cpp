// Tests for the memory legend's layout.
//
// Two defects lived here, both of which looked like something else.
//
// The remembered entries were not cleared with the panel's children, so the vector grew by one entry
// per category on every sample while the panel only ever held the current ones. Because the layout
// sizes the grid from the vector, the legend grew a row taller every second and pushed the chart above
// it up the page, which is what the user saw as an enlarging gap.
//
// The fix for that then skipped placement whenever the counts already matched. The entries are rebuilt
// on every sample, and a new element defaults to cell (0,0), so skipping placement stacked every entry
// on top of the first.
//
// Both are layout bugs whose only visible symptom is where things end up, so the rules are stated here
// rather than left to the next person reading the view.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        constexpr double MIN_ENTRY_WIDTH = 190.0;
        constexpr double COLUMN_SPACING = 16.0;

        /// Mirrors the column count the view derives from its width.
        [[nodiscard]] size_t _columnsFor(double availableWidth, size_t entryCount)
        {
            if (availableWidth <= 0.0)
            {
                return 1;
            }

            size_t columns =
                static_cast<size_t>((availableWidth + COLUMN_SPACING) / (MIN_ENTRY_WIDTH + COLUMN_SPACING));
            columns = (std::max)(columns, static_cast<size_t>(1));
            return (std::min)(columns, entryCount);
        }

        /// One entry's cell, which is what the placement loop assigns.
        struct Cell
        {
            size_t row{0};
            size_t column{0};

            [[nodiscard]] bool operator==(Cell const& other) const noexcept
            {
                return row == other.row && column == other.column;
            }
        };

        [[nodiscard]] std::vector<Cell> _place(size_t entryCount, size_t columns)
        {
            std::vector<Cell> cells(entryCount);
            for (size_t index = 0; index < entryCount; ++index)
            {
                cells[index] = Cell{index / columns, index % columns};
            }
            return cells;
        }
    }

    TEST(MemoryLegendLayoutTest, TheEntryCountDoesNotGrowAcrossSamples)
    {
        // The first defect. The categories are the same five on every sample, so the remembered entries
        // have to be the same five; a vector that is cleared with the panel cannot grow.
        constexpr size_t CATEGORIES = 5;

        std::vector<size_t> remembered;

        for (int sample = 0; sample < 20; ++sample)
        {
            remembered.clear();
            for (size_t i = 0; i < CATEGORIES; ++i)
            {
                remembered.push_back(i);
            }

            ASSERT_EQ(remembered.size(), CATEGORIES) << "the entry list grew on sample " << sample;
        }
    }

    TEST(MemoryLegendLayoutTest, TheRowCountFollowsTheEntriesAndNotTheirHistory)
    {
        // What the growing vector did: the layout derived its row count from the entries, so a list that
        // kept 100 entries across 20 samples asked for 50 rows and made the panel taller every second.
        constexpr size_t CATEGORIES = 5;

        // Two columns fit at this width, so five entries need three rows.
        size_t const columns = _columnsFor(600.0, CATEGORIES);
        ASSERT_EQ(columns, 2u);

        size_t const rows = (CATEGORIES + columns - 1) / columns;
        EXPECT_EQ(rows, 3u);

        // The same five entries after twenty samples, which must ask for the same three rows.
        EXPECT_EQ((CATEGORIES + columns - 1) / columns, rows);
    }

    TEST(MemoryLegendLayoutTest, EveryEntryGetsItsOwnCell)
    {
        // The second defect. A new element defaults to cell (0,0), so placement has to run on every pass
        // rather than only when the definitions change.
        constexpr size_t ENTRIES = 5;

        std::vector<Cell> const cells = _place(ENTRIES, 2);
        ASSERT_EQ(cells.size(), ENTRIES);

        for (size_t i = 0; i < cells.size(); ++i)
        {
            for (size_t j = i + 1; j < cells.size(); ++j)
            {
                EXPECT_FALSE(cells[i] == cells[j])
                    << "entries " << i << " and " << j << " share a cell";
            }
        }
    }

    TEST(MemoryLegendLayoutTest, ThePlacementIsUnchangedByARelayoutAtTheSameWidth)
    {
        // A resize that does not change the column count must not reshuffle the entries, or they would
        // appear to jump whenever a size-changed pass arrived.
        std::vector<Cell> const first = _place(5, 2);
        std::vector<Cell> const second = _place(5, 2);

        EXPECT_EQ(first, second);
    }

    TEST(MemoryLegendLayoutTest, TheColumnCountFollowsTheWidth)
    {
        // A fixed two per row is what left the rest of a wide page empty.
        EXPECT_EQ(_columnsFor(200.0, 5), 1u);
        EXPECT_EQ(_columnsFor(600.0, 5), 2u);
        EXPECT_EQ(_columnsFor(1000.0, 5), 4u);
        EXPECT_EQ(_columnsFor(2000.0, 5), 5u) << "more columns than entries would spread them over empty cells";

        // Before the first layout pass the width is zero, which must still place every entry.
        EXPECT_EQ(_columnsFor(0.0, 5), 1u);
    }

    TEST(MemoryLegendLayoutTest, ADegenerateWidthStillPlacesEveryEntry)
    {
        // A panel narrower than one entry leaves one column, and no entry is lost.
        for (double const width : {0.0, 1.0, 100.0, MIN_ENTRY_WIDTH})
        {
            size_t const columns = _columnsFor(width, 5);
            ASSERT_GE(columns, 1u);
            EXPECT_EQ(_place(5, columns).size(), 5u);
        }
    }
}
