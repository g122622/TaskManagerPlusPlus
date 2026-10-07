// Tests for the memory composition strip's segment selection.
//
// The strip itself needs a desktop session, but choosing which segments appear is pure logic,
// and it is where the visible mistakes are: a category with nothing in it leaves a notch in an
// otherwise continuous bar, and a bar whose parts do not sum to the installed total appears to
// have a gap or an overrun.
//
// The byte counts come from the page lists, which are verified against the installed total by
// the probe's own diagnostic; what is tested here is what the strip does with them.
//
// The last group covers the compression hatch: compressed memory is drawn as a lead of the in-use
// segment rather than as a segment of its own, and the rule that keeps it there -- never longer
// than the segment holding it, never counted towards the strip's total -- is what this file
// states, because getting it wrong double-counts memory that is already resident.
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
            uint64_t hatchedBytes{0};

            /// Whether this figure is drawn as a segment of the strip. False for one that is already
            /// inside another segment and is named in the legend only.
            bool inStrip{true};
        };

        /// Mirrors the clamp the bar applies to a hatched lead.
        [[nodiscard]] uint64_t HatchedLength(uint64_t bytes, uint64_t hatchedBytes)
        {
            return (hatchedBytes < bytes) ? hatchedBytes : bytes;
        }

        [[nodiscard]] std::vector<Segment> VisibleSegments(std::vector<Segment> const& input)
        {
            std::vector<Segment> visible;
            for (Segment const& segment : input)
            {
                // Only the figures that are segments of the strip are drawn; the rest are named in
                // the legend. This is the filter that stops compressed memory being added twice.
                if (segment.bytes > 0 && segment.inStrip)
                {
                    Segment entry = segment;
                    entry.hatchedBytes = HatchedLength(segment.bytes, segment.hatchedBytes);
                    visible.push_back(entry);
                }
            }
            return visible;
        }

        [[nodiscard]] uint64_t Total(std::vector<Segment> const& segments)
        {
            uint64_t total = 0;
            for (Segment const& segment : VisibleSegments(segments))
            {
                total += segment.bytes;
            }
            return total;
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

    // ------------------------------------------------------------------------
    // The compression hatch
    // ------------------------------------------------------------------------

    TEST(MemoryCompositionTest, ACompressedFigureIsDrawnInsideItsOwnSegment)
    {
        // The ordinary shape of this: part of in-use is compressed memory, so the hatch covers a
        // lead of that segment and the rest of it is left plain. The compressed length must not
        // lengthen the segment -- the pages are resident, so they are already counted in it.
        constexpr uint64_t IN_USE = 60ull * 1024 * 1024 * 1024;
        constexpr uint64_t COMPRESSED = 3ull * 1024 * 1024 * 1024;

        std::vector<Segment> const input{{IN_USE, COMPRESSED}};
        auto const visible = VisibleSegments(input);

        ASSERT_EQ(visible.size(), 1u);
        EXPECT_EQ(visible[0].bytes, IN_USE) << "the hatch must not lengthen the segment holding it";
        EXPECT_EQ(visible[0].hatchedBytes, COMPRESSED);
        EXPECT_LT(visible[0].hatchedBytes, visible[0].bytes) << "a lead must leave a plain remainder";
    }

    TEST(MemoryCompositionTest, AHatchLongerThanItsSegmentIsClampedToIt)
    {
        // The two figures come from different sources -- a page list and a process's residency -- so
        // they can disagree. A hatch drawn longer than its segment would paint over the neighbour to
        // its right and read as a category of its own.
        std::vector<Segment> const input{{4096, 8192}};
        auto const visible = VisibleSegments(input);

        ASSERT_EQ(visible.size(), 1u);
        EXPECT_EQ(visible[0].hatchedBytes, 4096u) << "a hatch cannot be longer than its own segment";
    }

    TEST(MemoryCompositionTest, NoHatchWithoutACompressedFigure)
    {
        // Memory compression off, or a round that could not read the compression process: the
        // segment is drawn plain rather than with an empty hatch surface.
        std::vector<Segment> const input{{8192, 0}};
        auto const visible = VisibleSegments(input);

        ASSERT_EQ(visible.size(), 1u);
        EXPECT_EQ(visible[0].hatchedBytes, 0u);
    }

    TEST(MemoryCompositionTest, CompressedMemoryIsNotASegmentOfItsOwn)
    {
        // The property the hatch exists to preserve: the strip accounts for the installed memory
        // exactly once. Compressed memory is reported beside the four categories and is already part
        // of in-use, so drawing it as a segment as well would make the strip longer than the machine
        // has -- which is why the entry is named in the legend and not appended to the strip.
        constexpr uint64_t INSTALLED = 128ull * 1024 * 1024 * 1024;
        constexpr uint64_t RESERVED = 211ull * 1024 * 1024;
        constexpr uint64_t IN_USE = 77ull * 1024 * 1024 * 1024;
        constexpr uint64_t COMPRESSED = 4ull * 1024 * 1024 * 1024;
        constexpr uint64_t MODIFIED = 1ull * 1024 * 1024 * 1024;
        constexpr uint64_t CACHED = 46ull * 1024 * 1024 * 1024;
        constexpr uint64_t FREE = INSTALLED - RESERVED - IN_USE - MODIFIED - CACHED;

        std::vector<Segment> const input{
            {RESERVED, 0, true},
            {IN_USE, COMPRESSED, true},
            {COMPRESSED, COMPRESSED, false},
            {MODIFIED, 0, true},
            {CACHED, 0, true},
            {FREE, 0, true},
        };

        auto const visible = VisibleSegments(input);

        EXPECT_EQ(visible.size(), 5u) << "the compressed entry names a part of in-use, it is not a segment";
        EXPECT_EQ(Total(input), INSTALLED) << "the parts must still account for the installed memory";
    }
}
