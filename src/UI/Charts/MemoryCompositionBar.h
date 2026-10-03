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
}
