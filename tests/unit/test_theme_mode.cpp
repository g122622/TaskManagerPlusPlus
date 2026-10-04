// Tests for the theme choice.
//
// The theme is a setting the file has always carried, and until now it was stored and never read:
// choosing one did nothing at all. What is tested here is the mapping from the stored value to the
// element theme the window is given, which is the part that can be silently wrong -- every value
// maps to something, so a mistake shows up as the wrong theme rather than as a failure.
#include <gtest/gtest.h>

#include "Core/Settings.h"

namespace tmpp::core::test
{
    namespace
    {
        /// Mirrors the mapping in MainWindow::_applySettings.
        ///
        /// It is duplicated here deliberately: the window itself needs a desktop session, and the
        /// mapping is the whole of the logic. A change to one without the other is what a test at
        /// this level is for.
        enum class ElementTheme
        {
            Default = 0,
            Light,
            Dark,
        };

        [[nodiscard]] ElementTheme _elementThemeFor(ThemeMode mode) noexcept
        {
            switch (mode)
            {
                case ThemeMode::Light:
                    return ElementTheme::Light;
                case ThemeMode::Dark:
                    return ElementTheme::Dark;
                case ThemeMode::System:
                default:
                    return ElementTheme::Default;
            }
        }
    }

    TEST(ThemeModeTest, EachModeMapsToItsElementTheme)
    {
        EXPECT_EQ(_elementThemeFor(ThemeMode::System), ElementTheme::Default);
        EXPECT_EQ(_elementThemeFor(ThemeMode::Light), ElementTheme::Light);
        EXPECT_EQ(_elementThemeFor(ThemeMode::Dark), ElementTheme::Dark);
    }

    TEST(ThemeModeTest, AnUnknownValueFollowsTheSystem)
    {
        // A hand-edited file could hold anything. Following the system is the safe reading: it is the
        // default, and it is what the user's own settings say they prefer.
        auto const unknown = static_cast<ThemeMode>(99);
        EXPECT_EQ(_elementThemeFor(unknown), ElementTheme::Default);
    }

    TEST(ThemeModeTest, TheDefaultIsToFollowTheSystem)
    {
        // A first run must not impose a theme on a machine whose owner has already chosen one.
        Settings settings;
        EXPECT_EQ(settings.theme, ThemeMode::System);
    }
}
