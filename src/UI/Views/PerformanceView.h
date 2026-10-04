// The Performance page.
//
// Layout matches Windows 11 Task Manager: a list of hardware sections on the left, and
// the selected section's content on the right. The sidebar rows carry a name, a
// qualifier and a small live chart, which is how the original communicates every
// section's state at a glance without requiring the user to click through them.
//
// The page is laid out with star sizing. A ScrollViewer gives its content unlimited
// height, so a star row inside one collapses to its content size -- putting the detail
// area in a ScrollViewer is what previously clipped the charts and pushed the details
// card out of view. The detail host is therefore a plain Grid.
//
// The sidebar is a stack of buttons rather than a ListView. Container population
// through ContainerContentChanging depends on the framework's container lifecycle, and
// on this build the rows were never realised at all -- the list drew empty.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "Core/Settings.h"
#include "UI/Views/CpuPage.h"
#include "UI/Views/DiskPage.h"
#include "UI/Views/GpuPage.h"
#include "UI/Views/MemoryPage.h"
#include "UI/Views/NetworkPage.h"
#include "UI/Charts/Sparkline.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the performance page.
     */
    class PerformanceView
    {
    public:
        /**
         * @param coordinator Supplies snapshots; owned by the application.
         * @param settings The loaded settings, for the sidebar width and the chart styles.
         * @param onSidebarWidthChanged Called when the user drags the sidebar, so the application can
         *        persist the new width. The view does not write settings itself: the application owns
         *        the file and decides when to save.
         */
        PerformanceView(core::SamplingCoordinator& coordinator,
                        core::Settings const& settings,
                        std::function<void(double)> onSidebarWidthChanged);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls a new sample and updates the charts. Cheap when nothing changed.
        void Refresh();

        /**
         * @brief Adopts changed settings.
         *
         * Applies the per-metric chart colours and line widths, so a change made on the settings page
         * is visible without a restart.
         */
        void ApplySettings(core::Settings const& settings);

    private:
        /**
         * @brief Which kind of page a sidebar row opens.
         *
         * The row list is built from the machine rather than fixed, because a machine can have any
         * number of disks: a fixed set showed one disk row and hid the rest, which reads as though
         * the other devices did not exist.
         */
        enum class SectionKind
        {
            Cpu,
            Memory,
            Disk,
            Network,
            Gpu,
        };

        /// Description of one sidebar row, so the list and the detail area agree.
        struct SectionSpec
        {
            /// The title shown in the row and in the page heading. A string rather than a literal
            /// because a disk's title is built from the volumes the device backs.
            std::wstring title;

            wchar_t const* glyph; ///< Segoe Fluent Icons glyph.
            SectionKind kind;
            bool hasData;

            /// Which device or adapter this row reports, for the kinds that have more than one.
            /// Unused by CPU, memory and GPU, and zero for those.
            size_t subIndex{0};
        };

        /// One sidebar row: its parts are retained so values can be updated in place.
        struct SidebarRow
        {
            winrt::Microsoft::UI::Xaml::Controls::Button button{nullptr};

            /// Which page this row opens, so a row's target survives the list being rebuilt.
            SectionKind kind{SectionKind::Cpu};

            /// Which device or adapter the row reports.
            size_t subIndex{0};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock title{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock subtitle{nullptr};
            std::unique_ptr<Sparkline> sparkline;
        };

        void _buildLayout();

        /// Applies a dragged width, clamped so the sidebar cannot be collapsed to nothing or grown
        /// past the detail area.
        void _setSidebarWidth(double width);

        /// Rebuilds the detail area for a section.
        /// Opens the page a row refers to.
        void _selectRow(size_t rowIndex);

        /// Applies the selected/unselected styling to every sidebar row.
        void _updateSelectionVisuals();

        /// The configured colour for a page, by the kind of metric it shows.
        [[nodiscard]] winrt::Windows::UI::Color _sectionColor(SectionKind kind) const;

        /// Feeds the sidebar rows from the current sample.
        void _updateSidebarValues(domain::SystemView const& system, domain::HistoryView const& history);

        /**
         * @brief Rebuilds the sidebar from the devices the machine actually has.
         *
         * The list is not fixed because a machine can have any number of disks. Rebuilding is driven
         * by the sample that first reports them, and does nothing once the list matches, so a row's
         * selection is not disturbed on every frame.
         *
         * @return True when the list changed.
         */
        bool _rebuildSidebarIfNeeded(domain::SystemView const& system);

        core::SamplingCoordinator& m_coordinator;

        /// The settings as loaded, for the sidebar width and per-metric chart styles.
        core::Settings m_settings;

        /// Called when the user drags the sidebar, so the width can be persisted.
        std::function<void(double)> m_onSidebarWidthChanged;

        /// The sidebar's current width, clamped on every change.
        double m_sidebarWidth{0.0};

        /// The drag handle at the sidebar's right edge.
        winrt::Microsoft::UI::Xaml::Controls::Border m_sidebarSplitter{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_sidebar{nullptr};

        /// Detail area, rebuilt per section.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_detailHost{nullptr};

        std::vector<SidebarRow> m_rows;

        /// The rows as they currently stand, so a rebuild can be skipped when nothing changed.
        std::vector<SectionSpec> m_sections;

        /// The CPU section's page. Retained across selections so switching away and back
        /// does not discard the per-core history it is displaying.
        std::unique_ptr<CpuPage> m_cpuPage;

        /// The Memory section's page. Retained for the same reason.
        std::unique_ptr<MemoryPage> m_memoryPage;

        /// One page per disk, indexed by the device's position in the sample. A machine has any
        /// number of disks and each gets its own page, so this is not a single member.
        std::vector<std::unique_ptr<DiskPage>> m_diskPages;

        /// The network page. Created on first selection and reused.
        std::unique_ptr<NetworkPage> m_networkPage;

        /// The GPU page. Created on first selection and reused.
        std::unique_ptr<GpuPage> m_gpuPage;

        /// How many device rows the list carries, taken from the last sample that reported any.
        ///
        /// The list is built from these rather than from the current sample, so a sample that reports no
        /// devices does not remove every disk and network row. That is a failed read, not an observation
        /// that the hardware is gone.
        size_t m_knownDiskCount{0};
        size_t m_knownNetworkCount{0};

        /// The last title seen for each device, so a row keeps its name while its figures are
        /// unavailable.
        std::map<size_t, std::wstring> m_diskTitles;
        std::map<size_t, std::wstring> m_networkTitles;

        /// Which row is open, as an index into m_rows. Reset when the list is rebuilt.
        size_t m_selectedRow{0};

        /// Which kind of metric is open, so the refresh path knows which page to drive.
        SectionKind m_selectedKind{SectionKind::Cpu};

        /// Which device or adapter is open, for the kinds that have more than one.
        size_t m_selectedSubIndex{0};

        /// Detail card for sections that have data but no dedicated page yet.
        winrt::Microsoft::UI::Xaml::Controls::Border m_detailsCard{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_detailValues;

        uint64_t m_renderedVersion{0};
    };
}