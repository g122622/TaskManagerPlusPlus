#include <gtest/gtest.h>
#include "UI/Lists/ProcessColumnLayout.h"

namespace tmpp::ui::test
{
    TEST(ProcessColumnLayoutTest, NormalizationPreservesValidOrderAndAppendsNewColumns)
    {
        EXPECT_EQ(columns::NormalizeOrder({3, 1, 3, 99, 0}, 5), (std::vector<uint32_t>{3, 1, 0, 2, 4}));
        EXPECT_EQ(columns::NormalizeOrder({}, 3), (std::vector<uint32_t>{0, 1, 2}));
    }

    TEST(ProcessColumnLayoutTest, DropsWorkOnEitherSideInBothDirections)
    {
        std::vector<uint32_t> order{0, 1, 2, 3};
        ASSERT_TRUE(columns::Move(order, 3, 0, false));
        EXPECT_EQ(order, (std::vector<uint32_t>{3, 0, 1, 2}));
        ASSERT_TRUE(columns::Move(order, 3, 1, true));
        EXPECT_EQ(order, (std::vector<uint32_t>{0, 1, 3, 2}));
        ASSERT_TRUE(columns::Move(order, 0, 2, false));
        EXPECT_EQ(order, (std::vector<uint32_t>{1, 3, 0, 2}));
        ASSERT_TRUE(columns::Move(order, 2, 1, true));
        EXPECT_EQ(order, (std::vector<uint32_t>{1, 2, 3, 0}));
    }

    TEST(ProcessColumnLayoutTest, UnchangedOrInvalidDropsDoNotReportChanges)
    {
        std::vector<uint32_t> order{0, 1, 2};
        EXPECT_FALSE(columns::Move(order, 0, 0, false));
        EXPECT_FALSE(columns::Move(order, 0, 1, false));
        EXPECT_FALSE(columns::Move(order, 2, 1, true));
        EXPECT_FALSE(columns::Move(order, 9, 1, true));
        EXPECT_FALSE(columns::Move(order, 1, 9, true));
        EXPECT_EQ(order, (std::vector<uint32_t>{0, 1, 2}));
    }

    TEST(ProcessColumnLayoutTest, ReorderingRetainsEveryLogicalColumnIncludingHiddenOnes)
    {
        auto order = columns::NormalizeOrder({7, 0, 8});
        for (uint32_t source = 0; source < columns::Count; ++source)
            for (uint32_t target = 0; target < columns::Count; ++target)
            {
                (void)columns::Move(order, source, target, source % 2 == 0);
                auto sorted = order;
                std::sort(sorted.begin(), sorted.end());
                EXPECT_EQ(sorted, columns::NormalizeOrder({}));
            }
    }
}
