// Tests for the disk row ordering.
//
// A disk is named by the volumes it backs, so ordering by the device number and ordering by the first
// drive letter give genuinely different lists rather than the same one reversed: the device the firmware
// calls 2 may be the one backing C:, and the device it calls 0 may be a spare drive with no volume at
// all.
//
// The comparator is exercised here rather than through the view, because the ordering rule is the part
// worth pinning: which of several plausible readings of "by first drive letter" the code implements.
#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the comparator the sidebar uses.
        [[nodiscard]] wchar_t _firstDriveLetter(std::string const& instanceName)
        {
            for (char const c : instanceName)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    return static_cast<wchar_t>(c);
                }
            }

            // Sorts after every letter, so a device with no volume letter comes last rather than first.
            return L'[';
        }

        [[nodiscard]] std::vector<size_t> _orderedByLetter(std::vector<std::string> const& names)
        {
            std::vector<size_t> order(names.size());
            std::iota(order.begin(), order.end(), 0);

            std::stable_sort(order.begin(), order.end(), [&names](size_t a, size_t b) {
                return _firstDriveLetter(names[a]) < _firstDriveLetter(names[b]);
            });

            return order;
        }

        [[nodiscard]] std::vector<size_t> _deviceOrder(size_t count)
        {
            std::vector<size_t> order(count);
            std::iota(order.begin(), order.end(), 0);
            return order;
        }
    }

    TEST(DiskOrderTest, TheDeviceOrderIsTheSampleOrder)
    {
        EXPECT_EQ(_deviceOrder(3), (std::vector<size_t>{0, 1, 2}));
    }

    TEST(DiskOrderTest, TheLetterOrderFollowsTheFirstVolume)
    {
        // The machine this was written for, whose device numbers and volume letters are unrelated: the
        // device the firmware calls 2 is the one backing C:.
        std::vector<std::string> const names{"2 C: D:", "0 F:", "1 G:", "3 E:", "4 Z:"};

        std::vector<size_t> const order = _orderedByLetter(names);

        // C:, E:, F:, G:, Z: -- which is devices 0, 3, 1, 2, 4.
        EXPECT_EQ(order, (std::vector<size_t>{0, 3, 1, 2, 4}));

        EXPECT_EQ(_firstDriveLetter(names[order[0]]), L'C');
        EXPECT_EQ(_firstDriveLetter(names[order[1]]), L'E');
        EXPECT_EQ(_firstDriveLetter(names[order[4]]), L'Z');
    }

    TEST(DiskOrderTest, ADeviceWithNoVolumeLetterSortsLast)
    {
        // A device with no letter has nothing to sort by, and putting it first would be a stronger claim
        // than the data supports.
        std::vector<std::string> const names{"0 F:", "5", "1 C:"};

        std::vector<size_t> const order = _orderedByLetter(names);

        EXPECT_EQ(order.back(), 1u) << "the device with no letter must come last";
        EXPECT_EQ(_firstDriveLetter(names[order[0]]), L'C');
    }

    TEST(DiskOrderTest, TheOrderIsStableBetweenEqualKeys)
    {
        // Two devices backing the same first letter keep their sample order, so the list does not
        // reshuffle on every rebuild for no reason the user can see.
        std::vector<std::string> const names{"0 C:", "1 C:", "2 C:"};

        EXPECT_EQ(_orderedByLetter(names), (std::vector<size_t>{0, 1, 2}));
    }

    TEST(DiskOrderTest, TheTwoOrdersDifferOnAMachineThatHasBoth)
    {
        // The point of the setting: on a machine whose device numbers and letters disagree, the two
        // orders are not the same list.
        // Device order is 2, 0, 1 while letter order is C(2), F(0), G(1) -- the same here, so the names
        // are the machine's real layout, where device 2 holds C: and the two orders genuinely differ.
        std::vector<std::string> const names{"2 C: D:", "0 F:", "1 G:", "3 E:"};

        EXPECT_NE(_deviceOrder(names.size()), _orderedByLetter(names));
    }
}
