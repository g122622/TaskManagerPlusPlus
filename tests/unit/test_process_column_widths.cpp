// Tests for the process list's column width rules.
//
// The widths are user-editable and persisted, which makes three things worth pinning:
//
//   * a drag cannot take a column below the width its own heading needs, or the column would become
//     unreadable and impossible to grab again;
//   * a width loaded from the settings file is discarded rather than clamped when it is out of range,
//     because a value that far out is a hand-edited or truncated file rather than a drag, and the
//     column's own default is a better answer than the nearest legal one;
//   * the total the row host is given includes the inset on both sides, or the rightmost column would be
//     clipped.
//
// The drag itself needs a desktop session; what is tested here is the arithmetic it drives.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the column table's width fields.
        struct ColumnSpec
        {
            double width;
            double minimumWidth;
        };

        constexpr std::array<ColumnSpec, 7> COLUMNS{{
            {300.0, 120.0}, // Name
            {80.0, 56.0},   // PID
            {90.0, 56.0},   // CPU
            {120.0, 72.0},  // Memory
            {100.0, 64.0},  // Disk
            {100.0, 64.0},  // Network
            {80.0, 56.0},   // GPU
        }};

        constexpr double ROW_INSET = 12.0;
        constexpr double MAX_COLUMN_WIDTH = 2000.0;

        /// Mirrors _setColumnWidth's clamp.
        [[nodiscard]] double ClampForDrag(size_t column, double requested)
        {
            return (std::max)(requested, COLUMNS[column].minimumWidth);
        }

        /// Mirrors SetColumnWidths' validation, which rejects rather than clamps.
        [[nodiscard]] bool AcceptsFromSettings(size_t column, double requested)
        {
            return requested >= COLUMNS[column].minimumWidth && requested <= MAX_COLUMN_WIDTH;
        }

        /// Mirrors _applyColumnWidths' total.
        [[nodiscard]] double RowWidthFor(std::array<double, 7> const& widths)
        {
            double total = 2.0 * ROW_INSET;
            for (double const width : widths)
            {
                total += width;
            }
            return total;
        }
    }

    TEST(ProcessColumnWidthsTest, ADragCannotShrinkAColumnBelowItsHeading)
    {
        // A column narrower than its own heading cannot be read, and one dragged to nothing could not be
        // grabbed again.
        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            EXPECT_DOUBLE_EQ(ClampForDrag(i, 0.0), COLUMNS[i].minimumWidth) << "column " << i;
            EXPECT_DOUBLE_EQ(ClampForDrag(i, -50.0), COLUMNS[i].minimumWidth) << "column " << i;
            EXPECT_DOUBLE_EQ(ClampForDrag(i, COLUMNS[i].minimumWidth - 1.0), COLUMNS[i].minimumWidth);
        }
    }

    TEST(ProcessColumnWidthsTest, ADragWiderThanTheFloorIsAppliedUnchanged)
    {
        // Widening is unbounded below the ceiling: a column dragged wide simply pushes the ones after it,
        // which is recoverable by dragging back.
        EXPECT_DOUBLE_EQ(ClampForDrag(0, 480.0), 480.0);
        EXPECT_DOUBLE_EQ(ClampForDrag(1, 200.0), 200.0);
        EXPECT_DOUBLE_EQ(ClampForDrag(6, 1200.0), 1200.0);
    }

    TEST(ProcessColumnWidthsTest, ASettingsValueOutOfRangeIsDiscardedRatherThanClamped)
    {
        // The distinction matters: clamping a hand-edited value would silently apply a width the user never
        // chose, whereas discarding it falls back to the column's own default, which is at least a width
        // that was designed for that column.
        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            EXPECT_TRUE(AcceptsFromSettings(i, COLUMNS[i].width)) << "the default must be accepted";
            EXPECT_TRUE(AcceptsFromSettings(i, COLUMNS[i].minimumWidth)) << "the floor itself is legal";

            EXPECT_FALSE(AcceptsFromSettings(i, 0.0)) << "column " << i;
            EXPECT_FALSE(AcceptsFromSettings(i, -1.0)) << "column " << i;
            EXPECT_FALSE(AcceptsFromSettings(i, COLUMNS[i].minimumWidth - 0.5)) << "column " << i;
            EXPECT_FALSE(AcceptsFromSettings(i, MAX_COLUMN_WIDTH + 1.0)) << "column " << i;
            EXPECT_FALSE(AcceptsFromSettings(i, 1.0e9)) << "column " << i;
        }
    }

    TEST(ProcessColumnWidthsTest, TheRowWidthIncludesBothInsets)
    {
        // The row's padding sits inside the width the host assigns it, so a canvas sized to the columns
        // alone would clip the rightmost one.
        std::array<double, 7> widths{};
        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            widths[i] = COLUMNS[i].width;
        }

        double expected = 2.0 * ROW_INSET;
        for (ColumnSpec const& column : COLUMNS)
        {
            expected += column.width;
        }

        EXPECT_DOUBLE_EQ(RowWidthFor(widths), expected);
        EXPECT_GT(RowWidthFor(widths), 7 * std::min(COLUMNS[0].width, 80.0)) << "the total must be a sum, not a count";
    }

    TEST(ProcessColumnWidthsTest, WideningOneColumnWidensTheRow)
    {
        // The host is told the new total on every drag, so widening a column has to move it.
        std::array<double, 7> widths{};
        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            widths[i] = COLUMNS[i].width;
        }

        double const before = RowWidthFor(widths);
        widths[0] += 100.0;
        EXPECT_DOUBLE_EQ(RowWidthFor(widths), before + 100.0);
    }

    TEST(ProcessColumnWidthsTest, AShortSettingsListLeavesTheRemainingColumnsAtTheirDefaults)
    {
        // A file written before a column was added has fewer entries than there are columns. The ones it
        // does not mention keep their defaults rather than becoming zero-width.
        std::vector<double> const fromFile{400.0, 100.0};
        std::array<double, 7> widths{};
        for (size_t i = 0; i < COLUMNS.size(); ++i)
        {
            widths[i] = COLUMNS[i].width;
        }

        for (size_t i = 0; i < widths.size() && i < fromFile.size(); ++i)
        {
            if (AcceptsFromSettings(i, fromFile[i]))
            {
                widths[i] = fromFile[i];
            }
        }

        EXPECT_DOUBLE_EQ(widths[0], 400.0) << "the name column was not adopted";
        EXPECT_DOUBLE_EQ(widths[1], 100.0) << "the pid column was not adopted";
        EXPECT_DOUBLE_EQ(widths[2], COLUMNS[2].width) << "an unmentioned column lost its default";
        EXPECT_DOUBLE_EQ(widths[6], COLUMNS[6].width);
    }
}
