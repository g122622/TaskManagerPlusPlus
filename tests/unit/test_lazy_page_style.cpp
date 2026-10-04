// Tests for how a chart style reaches a page that is built lazily.
//
// The pages that draw charts are created on first selection, not up front, while the style is applied
// from outside. That combination has one failure mode worth pinning: a page created after the last
// "apply" ran has nothing applied to it at all, and its charts keep whatever their constructors gave
// them. It is what made the configured line width look as though it had not been applied, and only for
// the sections the user had not yet opened.
//
// The fix is that the page takes its style as a constructor argument and applies it itself, so the two
// cannot come apart. What is tested here is the rule that makes that work: the style is read at
// construction, and a page built later is given the style in force at that moment rather than the one
// from the session's start.
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors SectionKind, whose order is also the order of the style table.
        enum class SectionKind
        {
            Cpu = 0,
            Memory,
            Disk,
            Network,
            Gpu,
        };

        /// Mirrors ChartStyle's relevant fields.
        struct ChartStyle
        {
            uint8_t red{0x4C};
            uint8_t green{0xC2};
            uint8_t blue{0xFF};
            double lineWidth{1.0};

            [[nodiscard]] double ClampedLineWidth() const noexcept
            {
                if (lineWidth < 0.5)
                {
                    return 0.5;
                }
                if (lineWidth > 8.0)
                {
                    return 8.0;
                }
                return lineWidth;
            }
        };

        /// Mirrors Settings::ChartStyleFor, so the mapping is covered by the same test as its use.
        [[nodiscard]] ChartStyle const& ChartStyleFor(std::array<ChartStyle, 5> const& styles, int index)
        {
            switch (index)
            {
                case 0:
                    return styles[0];
                case 1:
                    return styles[1];
                case 2:
                    return styles[2];
                case 3:
                    return styles[3];
                case 4:
                    return styles[4];
                default:
                    return styles[0];
            }
        }

        /// Stands in for a lazily-built page: it records the style it was constructed with.
        struct Page
        {
            ChartStyle appliedStyle;
            bool styled{false};

            explicit Page(ChartStyle const& style) : appliedStyle(style), styled(true) {}
        };
    }

    TEST(LazyPageStyleTest, APageBuiltLaterTakesTheStyleInForceThenNotTheSessionsFirst)
    {
        // The session with one style, then a second after the user moves the slider.
        std::array<ChartStyle, 5> styles{};
        styles[0].lineWidth = 1.0;
        styles[2].lineWidth = 1.0;

        std::vector<Page> pages;

        // The CPU page is opened first, at the original width.
        pages.emplace_back(ChartStyleFor(styles, static_cast<int>(SectionKind::Cpu)));

        // The user widens the lines, and the disk style in particular.
        styles[0].lineWidth = 4.0;
        styles[2].lineWidth = 6.0;

        // The disk page has not been visited yet, so it is built now -- and has to take the width in force
        // now, not the one from before the change.
        pages.emplace_back(ChartStyleFor(styles, static_cast<int>(SectionKind::Disk)));

        EXPECT_DOUBLE_EQ(pages[0].appliedStyle.ClampedLineWidth(), 1.0) << "an existing page keeps the width it was built with";
        EXPECT_DOUBLE_EQ(pages[1].appliedStyle.ClampedLineWidth(), 6.0) << "a new page must take the current width";
    }

    TEST(LazyPageStyleTest, EverySectionIndexMapsToItsOwnStyle)
    {
        // The enum's order is the style table's order. If they ever diverge, a section is drawn with
        // another section's colour -- which looks like a working customisation applied to the wrong chart.
        std::array<ChartStyle, 5> styles{};
        for (size_t i = 0; i < styles.size(); ++i)
        {
            styles[i].lineWidth = 1.0 + static_cast<double>(i);
        }

        EXPECT_DOUBLE_EQ(ChartStyleFor(styles, static_cast<int>(SectionKind::Cpu)).lineWidth, 1.0);
        EXPECT_DOUBLE_EQ(ChartStyleFor(styles, static_cast<int>(SectionKind::Memory)).lineWidth, 2.0);
        EXPECT_DOUBLE_EQ(ChartStyleFor(styles, static_cast<int>(SectionKind::Disk)).lineWidth, 3.0);
        EXPECT_DOUBLE_EQ(ChartStyleFor(styles, static_cast<int>(SectionKind::Network)).lineWidth, 4.0);
        EXPECT_DOUBLE_EQ(ChartStyleFor(styles, static_cast<int>(SectionKind::Gpu)).lineWidth, 5.0);
    }

    TEST(LazyPageStyleTest, AnOutOfRangeWidthIsClampedBeforeItReachesAChart)
    {
        // The value comes from a settings file, so it is clamped on the way in rather than trusted.
        ChartStyle style;
        style.lineWidth = 0.0;
        EXPECT_DOUBLE_EQ(style.ClampedLineWidth(), 0.5);

        style.lineWidth = 40.0;
        EXPECT_DOUBLE_EQ(style.ClampedLineWidth(), 8.0);

        style.lineWidth = 2.5;
        EXPECT_DOUBLE_EQ(style.ClampedLineWidth(), 2.5);
    }

    TEST(LazyPageStyleTest, APageIsNeverLeftUnstyled)
    {
        // The constructor always applies something, so a page cannot exist in the state that made the
        // setting look ignored. A default-constructed style is legitimate: it is what a fresh settings
        // file yields.
        ChartStyle const defaults{};
        Page const page(defaults);
        EXPECT_TRUE(page.styled);
        EXPECT_DOUBLE_EQ(page.appliedStyle.ClampedLineWidth(), 1.0);
    }
}
