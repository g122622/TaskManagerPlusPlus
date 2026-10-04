// Tests for the process icon reader.
//
// Icons come from the shell, so the values cannot be asserted exactly: which icon a path resolves to
// depends on the file type registration and the shell's own cache. What can be checked is that a real
// executable yields a real bitmap, that the pixels are shaped as the UI expects, and that the cases the
// shell cannot answer are reported as failures rather than as empty icons.
#include <gtest/gtest.h>

#include <windows.h>

#include <cstdio>
#include <string>

#include "Platform/Windows/ProcessIconReader.h"

namespace tmpp::platform::test
{
    namespace
    {
        /// A process that certainly has an icon: this test binary's own executable.
        [[nodiscard]] uint32_t _ownPid() noexcept
        {
            return ::GetCurrentProcessId();
        }
    }

    TEST(ProcessIconReaderTest, ReadsAnIconForARealExecutable)
    {
        ProcessIconReader reader;
        auto const icon = reader.Read(_ownPid());

        ASSERT_TRUE(icon.Success()) << icon.GetError().Message();

        IconPixels const& pixels = icon.Value();

        std::printf("\n--- process icon ---\n");
        std::printf("size: %ux%u, %zu bytes\n", pixels.width, pixels.height, pixels.bgra.size());

        // The shell's small image list is 16 by 16. A different size is not a failure -- the shell
        // decides -- but it must be square and non-empty.
        EXPECT_GT(pixels.width, 0u);
        EXPECT_EQ(pixels.width, pixels.height) << "an icon must be square";
        EXPECT_EQ(pixels.bgra.size(), static_cast<size_t>(pixels.width) * pixels.height * 4)
            << "the pixel buffer must be exactly width * height * 4 bytes of BGRA";

        // At least one pixel must be visible. A bitmap that is entirely transparent is what the shell
        // returns for some pseudo-icons, and drawing it would leave an invisible gap in the row.
        bool anyVisible = false;
        for (size_t i = 3; i < pixels.bgra.size(); i += 4)
        {
            if (pixels.bgra[i] != 0)
            {
                anyVisible = true;
                break;
            }
        }
        EXPECT_TRUE(anyVisible) << "the icon has no opaque pixels";
    }

    TEST(ProcessIconReaderTest, AFailedLookupIsReportedAsFailure)
    {
        ProcessIconReader reader;

        // Process id zero is the idle process, which has no executable file. The shell cannot resolve it,
        // so this must be a failure rather than a zeroed icon: the caller caches the two differently, and
        // an empty icon would draw as a blank space where every other row has an image.
        auto const icon = reader.Read(0);
        EXPECT_FALSE(icon.Success()) << "the idle process cannot have an icon";
    }

    TEST(ProcessIconReaderTest, PseudoProcessesAreRecognisedWithoutAskingTheShell)
    {
        // Each of these is a kernel pseudo-process with no file behind it. Asking the shell about them
        // fails every time, so recognising them keeps that work off the render path.
        EXPECT_TRUE(ProcessIconReader::HasNoImage(""));
        EXPECT_TRUE(ProcessIconReader::HasNoImage("System"));
        EXPECT_TRUE(ProcessIconReader::HasNoImage("Registry"));
        EXPECT_TRUE(ProcessIconReader::HasNoImage("Memory Compression"));
        EXPECT_TRUE(ProcessIconReader::HasNoImage("Secure System"));

        // A real image name is not one of them, or no row would ever get an icon.
        EXPECT_FALSE(ProcessIconReader::HasNoImage("chrome.exe"));
        EXPECT_FALSE(ProcessIconReader::HasNoImage("explorer.exe"));
    }

    TEST(ProcessIconReaderTest, ReadingTheSameIconTwiceIsConsistent)
    {
        // The reader does no caching of its own, so two calls must agree. A difference would make the
        // caller's cache store one of two possible answers at random.
        ProcessIconReader reader;

        auto const first = reader.Read(_ownPid());
        auto const second = reader.Read(_ownPid());

        ASSERT_TRUE(first.Success());
        ASSERT_TRUE(second.Success());

        EXPECT_EQ(first.Value().width, second.Value().width);
        EXPECT_EQ(first.Value().height, second.Value().height);
        EXPECT_EQ(first.Value().bgra, second.Value().bgra);
    }
}
