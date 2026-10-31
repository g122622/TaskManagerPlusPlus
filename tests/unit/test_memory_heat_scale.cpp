#include <gtest/gtest.h>
#include "UI/Lists/MemoryHeatScale.h"

namespace tmpp::ui::test
{
    TEST(MemoryHeatScaleTest, CompressionAtRankOneUsesTheLargestRegularProcessAsTheScale)
    {
        MemoryHeatScale scale;
        scale.Add(9300, "Memory Compression");
        scale.Add(1300, "browser.exe");
        scale.Add(800, "editor.exe");
        EXPECT_EQ(scale.Peak(), 1300u);
    }

    TEST(MemoryHeatScaleTest, CompressionBelowRankOneUsesTheNormalScale)
    {
        MemoryHeatScale scale;
        scale.Add(800, "Memory Compression");
        scale.Add(1300, "browser.exe");
        scale.Add(600, "editor.exe");
        EXPECT_EQ(scale.Peak(), 1300u);
        EXPECT_LT(800.0 / scale.Peak(), 1.0);
    }

    TEST(MemoryHeatScaleTest, FilteredAndEmptyListsHaveADefinedScale)
    {
        MemoryHeatScale scale;
        EXPECT_EQ(scale.Peak(), 0u);
        scale.Add(9300, "MEMORY COMPRESSION");
        EXPECT_EQ(scale.Peak(), 9300u);
        scale.Add(12000, "memory compression.exe");
        EXPECT_EQ(scale.Peak(), 12000u);
    }
}
