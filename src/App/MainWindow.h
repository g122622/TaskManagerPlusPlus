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
#include "UI/Views/PerformanceView.h"
#include "UI/Views/SettingsPage.h"
#include "UI/Views/ProcessesView.h"

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

        /**
         * @brief Persists everything the session changed.
         *
         * Called on close. It writes the current settings rather than the ones loaded at startup, so
         * a page selection, a sidebar width or a collapsed rail is not silently discarded.
         */
        void _saveSettingsOnClose();

        /**
         * @brief Adopts settings edited on the settings page.
         *
         * Applies the ones whose effect is expected immediately, and records the rest for the next
         * launch.
         */
        void _applySettings(core::Settings const& updated);

        /// Repositions the navigation rail's resize handle over the pane's right edge.
        void _updateNavigationSplitter();

        /**
         * @brief Whether a saved window rectangle would still be visible on some display.
         *
         * A remembered position can refer to a monitor that is no longer attached, or to a
         * larger desktop than the current one. Restoring it blindly places the window where
         * the user cannot see it, which presents as the application failing to start.
         *
         * @param x Left edge of the saved position.
         * @param y Top edge of the saved position.
         * @param width Saved window width, used to check that some of it remains on screen.
         * @param height Saved window height.
         */
        [[nodiscard]] static bool _isPositionVisible(int32_t x, int32_t y, int32_t width, int32_t height) noexcept;

        winrt::Microsoft::UI::Xaml::Controls::Grid m_rootGrid{nullptr};

        /// The app title bar: carries the application name and reserves the region the
        /// window buttons occupy. This is the element nominated as the title bar, so no
        /// page content is drawn under the buttons.
        winrt::Microsoft::UI::Xaml::Controls::Grid m_titleBarSpacer{nullptr};

        /// The page header bar's title, updated as the navigation selection changes so the
        /// bar names the page being shown.
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_pageTitle{nullptr};

        winrt::Microsoft::UI::Xaml::Controls::NavigationView m_navigation{nullptr};

        /// The navigation rail's resize handle. A NavigationView has no splitter, so this is an
        /// overlaid strip at the pane's right edge.
        winrt::Microsoft::UI::Xaml::Controls::Border m_navigationSplitter{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Grid m_contentHost{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::TextBlock m_statusText{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::InfoBar m_permissionBar{nullptr};

        winrt::Microsoft::UI::Dispatching::DispatcherQueueTimer m_refreshTimer{nullptr};

        /// Kept so window state can be saved on close. Null when the storage paths
        /// could not be resolved, in which case nothing is persisted.
        std::unique_ptr<core::SettingsStore> m_settingsStore;

        /// Settings as loaded at startup.
        core::Settings m_loadedSettings;

        /// Settings as they currently stand, updated as the user changes things. Written on close,
        /// so the session's choices survive. Save writes the whole document, which is why a single
        /// copy has to be kept current rather than rebuilt from the loaded one.
        core::Settings m_currentSettings;

        /// The page currently shown, recorded so LastUsed can resume it.
        core::StartupPage m_currentPage{core::StartupPage::Processes};

        /// The page index chosen at startup from the settings, applied once the views exist.
        int32_t m_startupPageIndex{0};

        /// The application-wide sampling owner. Declared before the views so it
        /// outlives them.
        std::unique_ptr<core::SamplingCoordinator> m_coordinator;
        std::unique_ptr<ui::ProcessesView> m_processesView;
        std::unique_ptr<ui::PerformanceView> m_performanceView;

        /// The settings page. Recreated on each visit so it reflects the settings in force.
        std::unique_ptr<ui::SettingsPage> m_settingsPage;

        /// The page currently shown, for the refresh timer to update.
        ui::ProcessesView* m_activeProcessesView{nullptr};
        ui::PerformanceView* m_activePerformanceView{nullptr};
    };
}
