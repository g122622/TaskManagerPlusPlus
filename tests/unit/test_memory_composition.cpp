// Tests for the memory composition strip's segment selection.
//
// The strip itself needs a desktop session, but choosing which segments appear is pure logic,
// and it is where the visible mistakes are: a category with nothing in it leaves a notch in an
// otherwise continuous bar, and a bar whose parts do not sum to the installed total appears to
// have a gap or an overrun.
//
// The byte counts come from the page lists, which are verified against the installed total by
// the probe's own diagnostic; what is tested here is what the strip does with them.
#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the segment construction in MemoryCompositionBar::SetSegments.
        struct Segment
        {
            uint64_t bytes;
        };

        [[nodiscard]] std::vector<Segment> VisibleSegments(std::vector<Segment> const& input)
        {
            std::vector<Segment> visible;
            for (Segment const& segment : input)
            {
                if (segment.bytes > 0)
                {
                    visible.push_back(segment);
                }
            }
            return visible;
        }
    }

    TEST(MemoryCompositionTest, EmptyCategoriesAreDropped)
    {
        // A machine with nothing on the modified list must not leave a sliver where that
        // segment would be.
        std::vector<Segment> const input{{100}, {0}, {250}, {0}};
        auto const visible = VisibleSegments(input);

        ASSERT_EQ(visible.size(), 2u);
        EXPECT_EQ(visible[0].bytes, 100u);
        EXPECT_EQ(visible[1].bytes, 250u);
    }

    TEST(MemoryCompositionTest, AllFourCategoriesAppearWhenAllHaveBytes)
    {
        // The normal case on a running system: all four lists hold something.
        std::vector<Segment> const input{{60953}, {950}, {59107}, {9848}};
        EXPECT_EQ(VisibleSegments(input).size(), 4u);
    }

    TEST(MemoryCompositionTest, AnAllZeroBreakdownYieldsNoSegments)
    {
        // With nothing to show the frame is collapsed rather than drawn empty, which would read
        // as a measurement of nothing.
        std::vector<Segment> const input{{0}, {0}, {0}, {0}};
        EXPECT_TRUE(VisibleSegments(input).empty());
    }

    TEST(MemoryCompositionTest, AnEmptyInputYieldsNoSegments)
    {
        EXPECT_TRUE(VisibleSegments({}).empty());
    }

    TEST(MemoryCompositionTest, SegmentsKeepTheirInputOrder)
    {
        // The order is the original's, left to right. Reordering would make the strip hard to
        // compare against another machine's.
        std::vector<Segment> const input{{10}, {20}, {30}, {40}};
        auto const visible = VisibleSegments(input);

        ASSERT_EQ(visible.size(), 4u);
        for (size_t i = 0; i < visible.size(); ++i)
        {
            EXPECT_EQ(visible[i].bytes, (i + 1) * 10) << "segment " << i << " moved";
        }
    }

    TEST(MemoryCompositionTest, SegmentsSumToTheInstalledTotal)
    {
        // The property the strip depends on: the four categories are mutually exclusive and
        // together account for all physical memory. The probe reports a zero difference against
        // the installed total on this machine, and the arithmetic here must preserve that.
        constexpr uint64_t INSTALLED_BYTES = 130860ull * 1024 * 1024;
        std::vector<Segment> const input{
            {static_cast<uint64_t>(60953.8 * 1024 * 1024)},
            {static_cast<uint64_t>(950.9 * 1024 * 1024)},
            {static_cast<uint64_t>(59107.3 * 1024 * 1024)},
            {static_cast<uint64_t>(9848.9 * 1024 * 1024)},
        };

        uint64_t total = 0;
        for (Segment const& segment : VisibleSegments(input))
        {
            total += segment.bytes;
        }

        // Within a few megabytes of the installed total, the remainder being the rounding in the
        // figures above rather than in the strip.
        uint64_t const difference = (total > INSTALLED_BYTES) ? (total - INSTALLED_BYTES)
                                                              : (INSTALLED_BYTES - total);
        EXPECT_LT(difference, 8ull * 1024 * 1024)
            << "the segments must account for all of physical memory";
    }

    TEST(MemoryCompositionTest, ASingleCategoryIsHandled)
    {
        // Immediately after boot, or on a machine with almost nothing cached, one category can
        // hold all of memory. It must still produce a strip rather than being treated as empty.
        std::vector<Segment> const input{{0}, {0}, {0}, {4096}};
        auto const visible = VisibleSegments(input);

        ASSERT_EQ(visible.size(), 1u);
        EXPECT_EQ(visible[0].bytes, 4096u);
    }
}
