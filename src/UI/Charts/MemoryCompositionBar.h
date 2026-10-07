// A proportional strip showing how physical memory is distributed.
//
// Windows 11 Task Manager draws this under the memory chart: one continuous bar divided into
// in-use, modified, cached (standby) and free, with the parts proportional to their sizes. It
// is the clearest way to show the breakdown, because the relative areas are read instantly
// while four separate figures have to be compared in the head.
//
// The strip is drawn with star-sized columns rather than computed pixel widths. That is what
// keeps it correct at any width without a layout pass, and it means a category that happens to
// be zero collapses to nothing on its own instead of needing a special case.
//
// Compressed memory is shown as a hatched lead on the in-use segment rather than as a segment of
// its own: its pages are resident, so they are already inside the in-use total, and a fifth
// segment would count those bytes twice.
#pragma once

#include "UI/WinRTUI.h"

#include <cstdint>
#include <vector>

namespace tmpp::ui
{
    /**
     * @brief A segmented bar showing the memory breakdown.
     */
    class MemoryCompositionBar
    {
    public:
        /**
         * @brief One segment: a label, a colour and a byte count.
         */
        struct Segment
        {
            wchar_t const* label;
            winrt::Windows::UI::Color color;
            uint64_t bytes;

            /**
             * @brief Length of this segment's leading edge drawn with the compression hatch.
             *
             * The hatched part is a part of the segment, not a category beside it, which is what
             * makes it possible to show compressed memory without counting it twice: compressed
             * pages are resident, so they are already inside the in-use figure.
             *
             * Zero draws the segment plain. A value longer than the segment is clamped, since a
             * hatch wider than the segment it belongs to would paint over the neighbour to its
             * right and read as a category of its own.
             */
            uint64_t hatchedBytes{0};
        };

        MemoryCompositionBar();

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /**
         * @brief Replaces the segments.
         *
         * Segments with no bytes are dropped, so a category that is empty leaves no sliver. The
         * caller supplies them in display order, largest first is not required but matching the
         * original's order keeps the bar comparable between machines.
         */
        void SetSegments(std::vector<Segment> const& segments);

        /// Empties the bar, for when the breakdown is unavailable.
        void Clear();

        /// Number of segments currently drawn. Exposed for tests and diagnostics.
        [[nodiscard]] size_t SegmentCount() const noexcept { return m_visible.size(); }

    private:
        void _rebuild();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        /// Rounded container holding the segments, so their outer corners follow its radius.
        ///
        /// There is deliberately no outline around it: the strip is a filled bar rather than a
        /// plot.
        winrt::Microsoft::UI::Xaml::Controls::Border m_clip{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid m_segments{nullptr};

        std::vector<Segment> m_visible;
    };

    /**
     * @brief Builds a colour-filled surface overlaid with diagonal white stripes.
     *
     * The hatch is the mark of compressed memory, and it appears in two places: the lead of the
     * in-use segment and that figure's legend swatch. Building both through one factory is what
     * keeps them recognisably the same texture, and what keeps a swatch from needing its own
     * stripe geometry.
     *
     * The stripes are regenerated from the surface's own size, so the same factory serves a ten
     * pixel swatch and a full-height segment, and they stay the same width at any window size or
     * display scale.
     *
     * @param color Fill the stripes are drawn over. The strip passes the segment's own colour, so
     *        the hatched part stays part of that segment instead of reading as another category.
     */
    [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Canvas MakeHatchedSurface(winrt::Windows::UI::Color color);
}
