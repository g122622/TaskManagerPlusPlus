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
#include <memory>
#include <vector>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "Core/Settings.h"
#include "UI/Views/CpuPage.h"
#include "UI/Views/DiskPage.h"
#include "UI/Views/MemoryPage.h"
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
         * @brief Sections the left-hand list offers.
         *
         * Only the first two have probes today. The rest are listed because the
         * structure is what the user expects, and each states plainly that its metrics
         * are not collected yet rather than showing an empty chart, which would read as
         * a measurement of zero.
         */
        enum class Section
        {
            Cpu = 0,
            Memory,
            Disk,
            Network,
            Gpu,
            Count,
        };

        /// Description of one section, so the sidebar and the detail area agree.
        struct SectionSpec
        {
            wchar_t const* title;
            wchar_t const* glyph; ///< Segoe Fluent Icons glyph.
            bool hasData;
        };

        /// One sidebar row: its parts are retained so values can be updated in place.
        struct SidebarRow
        {
            winrt::Microsoft::UI::Xaml::Controls::Button button{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock title{nullptr};
            winrt::Microsoft::UI::Xaml::Controls::TextBlock subtitle{nullptr};
            std::unique_ptr<Sparkline> sparkline;
        };

        void _buildLayout();

        /// Applies a dragged width, clamped so the sidebar cannot be collapsed to nothing or grown
        /// past the detail area.
        void _setSidebarWidth(double width);

        /// Rebuilds the detail area for a section.
        void _selectSection(Section section);

        /// Applies the selected/unselected styling to every sidebar row.
        void _updateSelectionVisuals();

        /// The configured colour for a section, read from the settings rather than the spec table.
        [[nodiscard]] winrt::Windows::UI::Color _sectionColor(size_t index) const;

        /// Feeds the sidebar rows from the current sample.
        void _updateSidebarValues(domain::SystemView const& system, domain::HistoryView const& history);

        /// Writes the current values into the existing detail rows.
        void _updateDetails(domain::SystemView const& system, domain::HistoryView const& history);

        [[nodiscard]] static std::vector<SectionSpec> const& _sections();

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

        /// The CPU section's page. Retained across selections so switching away and back
        /// does not discard the per-core history it is displaying.
        std::unique_ptr<CpuPage> m_cpuPage;

        /// The Memory section's page. Retained for the same reason.
        std::unique_ptr<MemoryPage> m_memoryPage;

        /// The disk page. Created on first selection and reused, so its chart is not rebuilt.
        std::unique_ptr<DiskPage> m_diskPage;

        /// The label the disk page's heading uses, taken from the sidebar row.
        std::wstring m_diskLabel;

        /// Which physical device the disk row and page report. The sidebar lists one row for the
        /// first device; a machine with several would need a row each, which the section list does
        /// not yet carry.
        size_t m_diskRowIndex{0};

        /// Detail card for sections that have data but no dedicated page yet.
        winrt::Microsoft::UI::Xaml::Controls::Border m_detailsCard{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_detailValues;

        Section m_selected{Section::Cpu};
        uint64_t m_renderedVersion{0};
    };
}