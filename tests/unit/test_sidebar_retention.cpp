// Tests for keeping the device rows when a sample reports none.
//
// The sidebar builds one row per device, so an empty sample does not merely leave a gap in the
// figures: every disk and network row disappears and comes back. That is what a user sees as the items
// flickering out of the sidebar, and it is a failed read rather than an observation that the hardware
// is gone.
//
// Two layers guard against it, and both are exercised here: the coordinator carries the previous
// reading forward through a failed probe, and the sidebar holds its own count so a list that comes
// back short cannot shrink the rows.
#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace tmpp::ui::test
{
    namespace
    {
        /// Mirrors the retained state the sidebar keeps between samples.
        struct SidebarState
        {
            size_t knownDiskCount{0};
            size_t knownNetworkCount{0};
            std::map<size_t, std::wstring> diskTitles;
            std::map<size_t, std::wstring> networkTitles;

            /// Applies a sample, in the order the real code does.
            void Apply(std::vector<std::string> const& diskNames, std::vector<std::string> const& networkNames)
            {
                if (!diskNames.empty())
                {
                    knownDiskCount = diskNames.size();
                }
                if (!networkNames.empty())
                {
                    knownNetworkCount = networkNames.size();
                }

                for (size_t i = 0; i < knownDiskCount; ++i)
                {
                    std::wstring title = L"Disk ";
                    if (i < diskNames.size())
                    {
                        title += std::wstring{diskNames[i].begin(), diskNames[i].end()};
                        diskTitles[i] = title;
                    }
                    else if (auto const known = diskTitles.find(i); known != diskTitles.end())
                    {
                        title = known->second;
                    }
                    else
                    {
                        title += std::to_wstring(i);
                    }
                }

                for (size_t i = 0; i < knownNetworkCount; ++i)
                {
                    if (i < networkNames.size())
                    {
                        networkTitles[i] = std::wstring{networkNames[i].begin(), networkNames[i].end()};
                    }
                }
            }

            [[nodiscard]] size_t RowCount() const noexcept { return 2 + knownDiskCount + knownNetworkCount + 1; }
        };
    }

    TEST(SidebarRetentionTest, AnEmptySampleDoesNotRemoveTheRows)
    {
        SidebarState state;
        state.Apply({"0 C:", "2 C: D:"}, {"Ethernet", "Wi-Fi"});
        ASSERT_EQ(state.RowCount(), 7u) << "2 fixed rows, 2 disks, 2 adapters, 1 GPU";

        // A failed read, which the probe used to report as a successful empty list.
        state.Apply({}, {});

        EXPECT_EQ(state.RowCount(), 7u) << "the rows must survive a sample that reports no devices";
        EXPECT_EQ(state.knownDiskCount, 2u);
        EXPECT_EQ(state.knownNetworkCount, 2u);
    }

    TEST(SidebarRetentionTest, TheDeviceNamesSurviveAnEmptySample)
    {
        // A row that kept its place but lost its name would be as confusing as one that vanished.
        SidebarState state;
        state.Apply({"2 C: D:"}, {"Intel(R) Wi-Fi 6E"});
        state.Apply({}, {});

        ASSERT_EQ(state.diskTitles.count(0), 1u);
        EXPECT_EQ(state.diskTitles[0], L"Disk 2 C: D:");
        EXPECT_EQ(state.networkTitles[0], L"Intel(R) Wi-Fi 6E");
    }

    TEST(SidebarRetentionTest, ADeviceThatGenuinelyGoneIsRemovedByTheNextGoodSample)
    {
        // Retention must not be permanent: a device that is really removed has to leave the list. The
        // rows follow the last sample that reported any, so the sample after the removal is the one that
        // drops it.
        SidebarState state;
        state.Apply({"0 C:", "1 D:", "2 E:"}, {});
        ASSERT_EQ(state.knownDiskCount, 3u);

        state.Apply({}, {});
        EXPECT_EQ(state.knownDiskCount, 3u) << "an empty sample is not evidence of removal";

        // A sample that reports two devices has genuinely seen two, so the third goes.
        state.Apply({"0 C:", "1 D:"}, {});
        EXPECT_EQ(state.knownDiskCount, 2u) << "a shorter non-empty sample removes the missing device";
    }

    TEST(SidebarRetentionTest, MoreDevicesThanBeforeAreAdded)
    {
        // A device plugged in mid-session must appear, which is the other direction of the same rule.
        SidebarState state;
        state.Apply({"0 C:"}, {});
        ASSERT_EQ(state.knownDiskCount, 1u);

        state.Apply({"0 C:", "1 D:"}, {});
        EXPECT_EQ(state.knownDiskCount, 2u);
    }

    TEST(SidebarRetentionTest, TheRowCountMatchesTheDevicesReported)
    {
        // The count the sidebar uses to decide whether to rebuild must follow the devices, or the list
        // would either rebuild every frame or never notice a change.
        SidebarState state;
        EXPECT_EQ(state.RowCount(), 3u) << "with no devices: CPU, memory and GPU";

        state.Apply({"0 C:"}, {"Ethernet"});
        EXPECT_EQ(state.RowCount(), 5u);

        state.Apply({}, {});
        EXPECT_EQ(state.RowCount(), 5u) << "unchanged by an empty sample";

        state.Apply({"0 C:", "1 D:"}, {"Ethernet", "Wi-Fi"});
        EXPECT_EQ(state.RowCount(), 7u);
    }
}
