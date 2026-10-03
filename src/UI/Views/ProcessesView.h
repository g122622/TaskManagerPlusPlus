// The Processes page.
//
// Rows are hosted by RowHost, which builds visuals only for the rows in view. With
// hundreds of processes on a typical machine, creating a control tree per process
// up front would cost hundreds of milliseconds before the first frame; this keeps
// the work proportional to what is actually on screen.
#pragma once

#include <cstdint>
#include <memory>

#include "UI/WinRTUI.h"

#include "Core/SamplingCoordinator.h"
#include "UI/Lists/ProcessListModel.h"
#include "UI/Lists/RowHost.h"

namespace tmpp::ui
{
    /**
     * @brief Builds and owns the process list page.
     *
     * Not a WinRT runtime class: it composes controls rather than deriving from one,
     * which avoids needing an IDL for a class with no XAML counterpart.
     */
    class ProcessesView
    {
    public:
        explicit ProcessesView(core::SamplingCoordinator& coordinator);

        [[nodiscard]] winrt::Microsoft::UI::Xaml::Controls::Grid Root() const { return m_root; }

        /// Pulls a new snapshot and repaints. Cheap when nothing changed.
        void Refresh();

    private:
        void _buildLayout();

        /// Updates the summary line, which reflects the filter and is cheap.
        void _updateSummary();

        /**
         * @brief Creates an empty row.
         *
         * The row is a grid of text blocks in the fixed column order; values are
         * filled in by the binder, so the factory does not depend on which row it
         * will end up showing. That is what makes recycling possible.
         */
        [[nodiscard]] winrt::Microsoft::UI::Xaml::FrameworkElement _createRow();

        /// Writes one row's values from the current snapshot.
        void _bindRow(winrt::Microsoft::UI::Xaml::FrameworkElement const& element, uint32_t rowIndex);

        core::SamplingCoordinator& m_coordinator;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_root{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBox m_search{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_summary{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_emptyMessage{nullptr};

        std::unique_ptr<RowHost> m_rowHost;
        std::unique_ptr<ProcessListModel> m_listModel;

        /// The snapshot the rows are currently bound to. Held because binding needs
        /// the values; copying it is the expensive operation the version check in
        /// Refresh exists to avoid.
        domain::ProcessSnapshotView m_snapshot;

        ListQuery m_query;
        uint64_t m_renderedVersion{0};
    };
}
