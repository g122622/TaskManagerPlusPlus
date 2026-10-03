// The Performance page.
//
// Layout matches Windows 11 Task Manager: a narrow list of hardware sections on the
// left, and the selected section's charts plus hardware details on the right.
//
// The section list is a stack of buttons rather than a ListView. Container
// population through ContainerContentChanging depends on the framework's container
// lifecycle, and on this build the rows were never realised at all -- the list drew
// empty. Buttons own their content directly, so there is no lifecycle to depend on,
// and with five fixed rows virtualisation would buy nothing anyway.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "UI/HistoryChart.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the performance page.
     */
    class PerformanceView
    {
    public:
        explicit PerformanceView(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls a new sample and updates the charts. Cheap when nothing changed.
        void Refresh();

    private:
        /**
         * @brief Sections the left-hand list offers.
         *
         * Only the first two have probes today. The rest are listed because the
         * structure is what the user expects, and each states plainly that its
         * metrics are not collected yet rather than showing an empty chart, which
         * would read as a measurement of zero.
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

        void _buildLayout();
        void _selectSection(Section section);

        /// Highlights the selected section's button and clears the others.
        void _updateSelectionVisuals();

        /// Writes the current values into the existing detail rows.
        void _updateDetails(domain::SystemView const& system);

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_sectionButtons{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::StackPanel m_detailPanel{nullptr};

        /// One button per section, indexed by Section.
        std::vector<winrt::Microsoft::UI::Xaml::Controls::Button> m_buttons;

        std::unique_ptr<HistoryChart> m_primaryChart;
        std::unique_ptr<HistoryChart> m_secondaryChart;

        /// The details card, created once per section and whose value rows are
        /// updated in place. Rebuilding it per refresh would append controls several
        /// times a second and grow without bound.
        winrt::Microsoft::UI::Xaml::Controls::Border m_detailsCard{nullptr};
        std::vector<winrt::Microsoft::UI::Xaml::Controls::TextBlock> m_detailValues;

        Section m_selected{Section::Cpu};
        uint64_t m_renderedVersion{0};
    };
}
