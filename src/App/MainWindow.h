// Main application window.
//
// Owns the navigation shell, the pages, and a UI timer that pulls new samples. The
// sampling itself happens on the sampler thread; this timer only refreshes what is on
// screen, and each page's Refresh() is a no-op when the sampling thread has not
// published anything new.
#pragma once

#include "WinRT.h"

#include <memory>

#include <winrt/Microsoft.UI.Dispatching.h>

#include "Core/SamplingCoordinator.h"
#include "Core/Settings.h"
#include "UI/PerformanceView.h"
#include "UI/ProcessesView.h"

namespace tmpp
{
    /**
     * @brief Main application window.
     *
     * The control tree is created in C++ rather than in XAML, because this Visual
     * Studio installation has no native C++ XAML build support (docs/BUILD.md).
     *
     * NOTE: must not be final; the C++/WinRT activation factory derives from the
     * implementation type.
     */
    class MainWindow : public winrt::Microsoft::UI::Xaml::WindowT<MainWindow>
    {
    public:
        MainWindow();

    private:
        void _buildContent();
        void _createPages();
        void _selectPage(int32_t index);
        void _startRefreshTimer();
        void _updateStatusBar();

        winrt::Microsoft::UI::Xaml::Controls::Grid m_rootGrid{nullptr};

        /// Reserves the region the window buttons occupy. This is the element
        /// nominated as the title bar, so no page content is drawn under them.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_titleBarSpacer{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::NavigationView m_navigation{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid m_contentHost{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_statusText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::InfoBar m_permissionBar{nullptr};

        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_refreshTimer{nullptr};

        /// Kept so window state can be saved on close. Null when the storage paths
        /// could not be resolved, in which case nothing is persisted.
        std::unique_ptr<core::SettingsStore> m_settingsStore;

        /// Settings as loaded, so saving the window placement on close does not discard
        /// the user's other choices. Save writes the whole document.
        core::Settings m_loadedSettings;

        /// The application-wide sampling owner. Declared before the views so it
        /// outlives them.
        std::unique_ptr<core::SamplingCoordinator> m_coordinator;
        std::unique_ptr<ui::ProcessesView> m_processesView;
        std::unique_ptr<ui::PerformanceView> m_performanceView;

        /// The page currently shown, for the refresh timer to update.
        ui::ProcessesView* m_activeProcessesView{nullptr};
        ui::PerformanceView* m_activePerformanceView{nullptr};
    };
}
