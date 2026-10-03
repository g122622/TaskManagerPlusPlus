#include "WinRT.h"

#include "MainWindow.h"
#include "StartupLog.h"

#include "Core/Logging.h"
#include "Core/PathService.h"
#include "Core/Settings.h"
#include "UI/Controls.h"
#include "UI/Formatting.h"
#include "UI/Theme.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <chrono>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::ContentDialog;
using winrt::Microsoft::UI::Xaml::Controls::ContentDialogButton;
using winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult;
using winrt::Microsoft::UI::Xaml::Controls::FontIcon;
using winrt::Microsoft::UI::Xaml::Controls::Frame;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::InfoBar;
using winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity;
using winrt::Microsoft::UI::Xaml::Controls::NavigationView;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewBackButtonVisible;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewItem;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewPaneDisplayMode;
using winrt::Microsoft::UI::Xaml::Controls::ScrollViewer;
using winrt::Microsoft::UI::Xaml::Controls::ScrollBarVisibility;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;

namespace tmpp
{
    namespace
    {
        /// How often the UI polls the sampler for a new published snapshot. This is
        /// independent of the sampling interval: the poll is cheap and only reacts
        /// when something new has been published, so it can be frequent enough to
        /// feel immediate without doing any work.
        constexpr auto UI_REFRESH_INTERVAL = std::chrono::milliseconds(100);

        /// Directory name under %LOCALAPPDATA% and the settings file name.
        /// Narrow strings because PathService takes std::string_view: the
        /// application works in UTF-8 internally (see Platform/Windows/WindowsString.h).
        constexpr char const* APPLICATION_NAME = "TaskManagerPlusPlus";
        constexpr char const* SETTINGS_FILE_NAME = "settings.json";
    }

    MainWindow::MainWindow()
    {
        diag::LogStartup("MainWindow: ctor begin");

        Title(L"TaskManagerPlusPlus");

        // --- Settings and paths ------------------------------------------------
        core::Settings m_settings;
        auto const paths = core::PathService::Create(APPLICATION_NAME, SETTINGS_FILE_NAME);
        if (paths.Success())
        {
            core::SettingsStore store{paths.Value().SettingsFile()};
            m_settings = store.Load();
            if (store.DamagedFilePreserved())
            {
                diag::LogStartup("MainWindow: settings file was damaged and preserved as .bak");
            }
        }
        else
        {
            diag::LogStartup("MainWindow: could not resolve storage paths; using defaults");
        }
        diag::LogStartup("MainWindow: settings loaded");

        // --- Sampling ----------------------------------------------------------
        m_coordinator = std::make_unique<core::SamplingCoordinator>(m_settings.intervalMs);
        diag::LogStartup("MainWindow: coordinator constructed");

        // The shell is built before the pages, because selecting a navigation item
        // during construction needs the content host to already exist. Selecting a
        // page while the views are still null is handled by _selectPage.
        _buildContent();
        diag::LogStartup("MainWindow: content built");

        _createPages();
        diag::LogStartup("MainWindow: pages created");

        // Sampling starts only after the UI exists, so the first callback cannot
        // publish into a half-built window.
        m_coordinator->Start();
        diag::LogStartup("MainWindow: sampling started");

        _startRefreshTimer();

        // Restore the remembered window size. Position is applied by the platform
        // when it is positive, which is why "not yet decided" is stored as negative.
        // TODO: window position restoration is not applied yet; only the size is.
        if (m_settings.windowWidth > 0 && m_settings.windowHeight > 0)
        {
            if (auto const appWindow = this->AppWindow())
            {
                appWindow.Resize(winrt::Windows::Graphics::SizeInt32{m_settings.windowWidth, m_settings.windowHeight});
            }
        }

        diag::LogStartup("MainWindow: ctor end");
    }

    void MainWindow::_createPages()
    {
        m_processesView = std::make_unique<ui::ProcessesView>(*m_coordinator);
        m_performanceView = std::make_unique<ui::PerformanceView>(*m_coordinator);
        _selectPage(0);
    }

    void MainWindow::_selectPage(int32_t index)
    {
        // Selecting a navigation item during shell construction arrives before the
        // pages exist, so it is ignored; _createPages selects the default page once
        // the views are ready.
        if (m_contentHost == nullptr || m_processesView == nullptr || m_performanceView == nullptr)
        {
            return;
        }

        m_contentHost.Children().Clear();

        // Only one page renders at a time, so only the visible page is refreshed.
        // Refreshing a hidden page would do work whose result nobody sees.
        m_activeProcessesView = nullptr;
        m_activePerformanceView = nullptr;

        switch (index)
        {
            case 0:
                m_contentHost.Children().Append(m_processesView->Root());
                m_activeProcessesView = m_processesView.get();
                break;
            case 1:
                m_contentHost.Children().Append(m_performanceView->Root());
                m_activePerformanceView = m_performanceView.get();
                break;
            default:
            {
                // The Details page arrives in M2. The placeholder states that plainly
                // rather than showing an empty grid.
                StackPanel placeholder = ui::controls::MakeStack(8.0);
                ui::controls::ApplyPageMargin(placeholder);
                placeholder.Children().Append(ui::controls::MakeHeading(L"Details"));
                placeholder.Children().Append(ui::controls::MakeText(
                    L"The detailed process table with per-column customisation arrives in milestone M2. "
                    L"See docs/ROADMAP.md.",
                    13.0,
                    true));
                m_contentHost.Children().Append(placeholder);
                break;
            }
        }
    }

    void MainWindow::_buildContent()
    {
        // --- Navigation shell ---------------------------------------------------
        m_navigation = NavigationView();
        m_navigation.IsSettingsVisible(false);
        m_navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        m_navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        m_navigation.OpenPaneLength(ui::metrics::NAVIGATION_PANE_WIDTH);

        auto addItem = [this](wchar_t const* label, wchar_t const* glyph) {
            NavigationViewItem item;
            item.Content(winrt::box_value(winrt::hstring{label}));
            FontIcon icon;
            icon.Glyph(glyph);
            item.Icon(icon);
            m_navigation.MenuItems().Append(item);
            return item;
        };

        // Segoe Fluent Icons glyphs.
        auto processesItem = addItem(L"Processes", L"\xE9D9");
        addItem(L"Performance", L"\xE9D2");
        addItem(L"Details", L"\xE8FD");

        // Explicit parameter types rather than a generic lambda: with 'auto', the
        // try_as<...> call becomes a dependent template name and needs a 'template'
        // disambiguator. Naming the types is clearer and avoids that.
        m_navigation.SelectionChanged(
            [this](winrt::Windows::Foundation::IInspectable const& sender,
                   winrt::Microsoft::UI::Xaml::Controls::NavigationViewSelectionChangedEventArgs const&) {
                auto const view = sender.try_as<NavigationView>();
                if (view == nullptr)
                {
                    return;
                }

                auto const selected = view.SelectedItem();
                if (selected == nullptr)
                {
                    return;
                }

                // IVector::IndexOf reports the position through an out parameter and
                // returns a bool, rather than returning the index directly.
                uint32_t index = 0;
                if (view.MenuItems().IndexOf(selected, index))
                {
                    _selectPage(static_cast<int32_t>(index));
                }
            });

        m_navigation.SelectedItem(processesItem);

        // --- Status bar ---------------------------------------------------------
        Grid statusBar = Grid();
        statusBar.Height(ui::metrics::STATUS_BAR_HEIGHT);
        statusBar.Padding(ThicknessHelper::FromLengths(ui::metrics::PAGE_MARGIN, 0.0, ui::metrics::PAGE_MARGIN, 0.0));
        statusBar.Background(ui::controls::ThemedBrush(ui::theme::LAYER_BACKGROUND));

        m_statusText = ui::controls::MakeText(L"", 12.0, true);
        m_statusText.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Center);
        statusBar.Children().Append(m_statusText);

        // --- Permission notice --------------------------------------------------
        // Shown only when a capability is missing, so a fully privileged run shows
        // nothing. A modal dialog would be wrong here: the application still works,
        // it just shows less.
        m_permissionBar = InfoBar();
        m_permissionBar.Severity(InfoBarSeverity::Informational);
        m_permissionBar.Title(L"Limited data");
        m_permissionBar.Message(
            L"Some metrics are unavailable at the current privilege level. "
            L"I/O counters and process owners need elevation.");
        m_permissionBar.IsOpen(false);
        m_permissionBar.IsClosable(true);

        // --- Assembly -----------------------------------------------------------
        Grid contentColumn = Grid();
        contentColumn.RowDefinitions().Append(winrt::Microsoft::UI::Xaml::Controls::RowDefinition{});
        contentColumn.RowDefinitions().Append(winrt::Microsoft::UI::Xaml::Controls::RowDefinition{});
        contentColumn.RowDefinitions().Append(winrt::Microsoft::UI::Xaml::Controls::RowDefinition{});

        m_contentHost = Grid();
        Grid::SetRow(m_contentHost, 0);
        contentColumn.Children().Append(m_contentHost);

        Grid::SetRow(m_permissionBar, 1);
        contentColumn.Children().Append(m_permissionBar);

        Grid::SetRow(statusBar, 2);
        contentColumn.Children().Append(statusBar);

        m_navigation.Content(contentColumn);

        m_rootGrid = Grid();
        m_rootGrid.Background(ui::controls::ThemedBrush(ui::theme::PAGE_BACKGROUND));
        m_rootGrid.Children().Append(m_navigation);

        Content(m_rootGrid);

        // Extend into the title bar and make the navigation pane the drag region,
        // matching the Windows 11 Task Manager chrome.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(m_navigation);

        _updateStatusBar();
    }

    void MainWindow::_startRefreshTimer()
    {
        auto const queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_refreshTimer = queue.CreateTimer();
        m_refreshTimer.Interval(UI_REFRESH_INTERVAL);
        m_refreshTimer.IsRepeating(true);
        m_refreshTimer.Tick([this](auto const&, auto const&) {
            // Each page's Refresh is a no-op when nothing new was published, so this
            // can run frequently without cost.
            if (m_activeProcessesView != nullptr)
            {
                m_activeProcessesView->Refresh();
            }
            if (m_activePerformanceView != nullptr)
            {
                m_activePerformanceView->Refresh();
            }
            _updateStatusBar();
        });
        m_refreshTimer.Start();
    }

    void MainWindow::_updateStatusBar()
    {
        core::SamplingStatus const status = m_coordinator->Status();

        std::wstring text;
        text += std::to_wstring(m_coordinator->IntervalMs());
        text += L" ms interval";
        text += L"  \x2022  ";
        text += std::to_wstring(m_coordinator->LogicalProcessorCount());
        text += L" logical processors";
        text += L"  \x2022  ";
        text += std::to_wstring(m_coordinator->SampleCount());
        text += L" samples";

        // A run of failures is worth stating: silently showing stale numbers is the
        // worst outcome for a monitoring tool.
        if (status.consecutiveFailures > 0)
        {
            text += L"  \x2022  ";
            text += std::to_wstring(status.consecutiveFailures);
            text += L" consecutive sampling failures";
        }

        m_statusText.Text(winrt::hstring{text});

        // The notice appears only when process I/O counts are actually unavailable.
        auto const capabilities = m_coordinator->ProcessCapabilities();
        bool const missingData = !capabilities.hasIoCounters || !capabilities.hasCpuTimes;
        m_permissionBar.IsOpen(missingData);
    }
}
