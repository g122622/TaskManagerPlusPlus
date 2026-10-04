// Tests for the order a process tree is terminated in.
//
// The order is the whole of the correctness here, and it is invisible from the outside: a tree ended
// parents-first leaves the children running, because a process whose parent has gone is reparented
// rather than stopped. The tree would then be half-killed and the user would see the survivors still
// in the list.
//
// The termination itself needs real processes, so what is tested is the ordering rule the
// implementation follows, derived from a snapshot's parent links.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace tmpp::platform::test
{
    namespace
    {
        /// Mirrors the ordering in WindowsProcessActions::TerminateTree.
        ///
        /// Duplicated deliberately: the real one terminates as it walks, and the ordering is the part
        /// that can be wrong without any visible symptom until a real tree is ended.
        [[nodiscard]] std::vector<uint32_t> _terminationOrder(
            uint32_t root, std::vector<std::pair<uint32_t, uint32_t>> const& parentByPid)
        {
            std::map<uint32_t, std::vector<uint32_t>> childrenByParent;
            for (auto const& [child, parent] : parentByPid)
            {
                childrenByParent[parent].push_back(child);
            }

            std::vector<uint32_t> order;
            std::vector<uint32_t> pending{root};
            std::map<uint32_t, bool> visited;

            while (!pending.empty())
            {
                uint32_t const current = pending.back();
                pending.pop_back();

                if (visited[current])
                {
                    continue;
                }
                visited[current] = true;

                order.push_back(current);

                if (auto const found = childrenByParent.find(current); found != childrenByParent.end())
                {
                    for (uint32_t const child : found->second)
                    {
                        pending.push_back(child);
                    }
                }
            }

            std::reverse(order.begin(), order.end());
            return order;
        }

        [[nodiscard]] size_t _positionOf(std::vector<uint32_t> const& order, uint32_t pid)
        {
            auto const found = std::find(order.begin(), order.end(), pid);
            return (found == order.end()) ? order.size() : static_cast<size_t>(found - order.begin());
        }
    }

    TEST(ProcessTreeOrderTest, EveryChildIsEndedBeforeItsParent)
    {
        // 100 -> 200 -> 300, a straight chain.
        std::vector<std::pair<uint32_t, uint32_t>> const parents{{200, 100}, {300, 200}};
        auto const order = _terminationOrder(100, parents);

        ASSERT_EQ(order.size(), 3u);
        EXPECT_LT(_positionOf(order, 300), _positionOf(order, 200)) << "the grandchild must go first";
        EXPECT_LT(_positionOf(order, 200), _positionOf(order, 100)) << "the child must go before the root";
    }

    TEST(ProcessTreeOrderTest, SiblingsAreAllEndedBeforeTheirParent)
    {
        // 100 has three children, one of which has its own child.
        std::vector<std::pair<uint32_t, uint32_t>> const parents{
            {200, 100}, {300, 100}, {400, 100}, {500, 300}};
        auto const order = _terminationOrder(100, parents);

        ASSERT_EQ(order.size(), 5u);

        // The root is last, and so are all its children before it.
        EXPECT_EQ(order.back(), 100u);
        for (uint32_t const child : {200u, 300u, 400u})
        {
            EXPECT_LT(_positionOf(order, child), _positionOf(order, 100))
                << "child " << child << " must be ended before its parent";
        }

        // The grandchild precedes the child it belongs to.
        EXPECT_LT(_positionOf(order, 500), _positionOf(order, 300));
    }

    TEST(ProcessTreeOrderTest, ALeafEndsUpAlone)
    {
        // Ending a process with no children must produce exactly itself, not its parent or siblings.
        std::vector<std::pair<uint32_t, uint32_t>> const parents{{200, 100}, {300, 100}};
        auto const order = _terminationOrder(200, parents);

        ASSERT_EQ(order.size(), 1u);
        EXPECT_EQ(order[0], 200u);
    }

    TEST(ProcessTreeOrderTest, ACycleTerminates)
    {
        // A parent id can be reused, which puts a cycle in the snapshot. Without the visited set the
        // walk would not terminate, and the application would hang rather than end anything.
        std::vector<std::pair<uint32_t, uint32_t>> const parents{{200, 100}, {100, 200}};
        auto const order = _terminationOrder(100, parents);

        // Both appear exactly once and the walk finishes.
        EXPECT_EQ(order.size(), 2u);
        EXPECT_EQ(std::count(order.begin(), order.end(), 100u), 1);
        EXPECT_EQ(std::count(order.begin(), order.end(), 200u), 1);
    }

    TEST(ProcessTreeOrderTest, AnEmptySnapshotYieldsJustTheRoot)
    {
        // The parent links can be empty when the snapshot has been replaced, and the root must still
        // be ended.
        auto const order = _terminationOrder(4242, {});
        ASSERT_EQ(order.size(), 1u);
        EXPECT_EQ(order[0], 4242u);
    }

    TEST(ProcessTreeOrderTest, AWideTreeEndsEveryChildBeforeTheRoot)
    {
        // A hundred children: the interesting case is that none is missed, which a recursion with a
        // depth limit would do.
        std::vector<std::pair<uint32_t, uint32_t>> parents;
        for (uint32_t i = 0; i < 100; ++i)
        {
            parents.emplace_back(1000 + i, 999);
        }

        auto const order = _terminationOrder(999, parents);
        ASSERT_EQ(order.size(), 101u);
        EXPECT_EQ(order.back(), 999u);

        for (uint32_t i = 0; i < 100; ++i)
        {
            EXPECT_NE(_positionOf(order, 1000 + i), order.size()) << "child " << (1000 + i) << " was missed";
        }
    }
}
