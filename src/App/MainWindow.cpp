#include "WinRT.h"

#include "MainWindow.h"
#include "StartupLog.h"
#if defined(_DEBUG)
#include "../../tests/ui/ThemeVerification.h"
#endif
#include <winrt/Microsoft.UI.Interop.h>

#include "Core/Logging.h"
#include "Core/PathService.h"
#include "Core/Settings.h"
#include "UI/Theming/Controls.h"
#include "UI/Diagnostics.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/ResizeDrag.h"
#include "UI/Theming/Theme.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <shldisp.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using winrt::Microsoft::UI::Xaml::Controls::ColumnDefinition;
using winrt::Microsoft::UI::Xaml::Controls::FontIcon;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::InfoBar;
using winrt::Microsoft::UI::Xaml::Controls::InfoBarSeverity;
using winrt::Microsoft::UI::Xaml::Controls::NavigationView;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewBackButtonVisible;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewItem;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewPaneDisplayMode;
using winrt::Microsoft::UI::Xaml::Controls::RowDefinition;
using winrt::Microsoft::UI::Xaml::Controls::StackPanel;
using winrt::Microsoft::UI::Xaml::Controls::TextBlock;
using winrt::Microsoft::UI::Xaml::GridLengthHelper;
using winrt::Microsoft::UI::Xaml::GridUnitType;
using winrt::Microsoft::UI::Xaml::ThicknessHelper;
using winrt::Microsoft::UI::Xaml::VerticalAlignment;

namespace tmpp
{
    namespace
    {
        /// How often the UI polls the sampler for a new published snapshot. This is
        /// independent of the sampling interval: the poll is cheap and only reacts when
        /// something new has been published, so it can be frequent enough to feel
        /// immediate without doing any work.
        constexpr auto UI_REFRESH_INTERVAL = std::chrono::milliseconds(100);

        /// Directory name under %LOCALAPPDATA% and the settings file name.
        /// Narrow strings because PathService takes std::string_view: the application
        /// works in UTF-8 internally (see Platform/Windows/WindowsString.h).
        constexpr char const* APPLICATION_NAME = "TaskManagerPlusPlus";
        constexpr char const* SETTINGS_FILE_NAME = "settings.json";

        /// The file holding the per-executable tuning rules.
        ///
        /// A file of its own rather than a section of settings.json: rules are edited one at a time by
        /// an explicit act, outlive every other setting, and are the only part of the configuration a
        /// user is likely to write by hand and copy between machines. Keeping them apart also means a
        /// rule file that becomes unreadable cannot cost the user their window layout or chart colours.
        constexpr char const* PROCESS_RULES_FILE_NAME = "process-rules.json";

        /// Height of the custom title bar strip.
        ///
        /// The window buttons occupy a fixed region at the top right. Reserving its
        /// height here is the equivalent of a CSS calc(): the strip is out of the flow
        /// of the content rows below it, and nothing drawn there can land under the
        /// buttons. Without this the CPU percentage was drawn on top of them.
        constexpr double TITLE_BAR_HEIGHT = 40.0;

        // One native title bar aligns the app identity, page, search and actions with the caption buttons.
        constexpr double APP_TITLE_BAR_HEIGHT = 48.0;

        /// Index used for the Settings page. Outside the range a navigation item can produce, so it
        /// cannot collide with a page the user selected from the main list.
        constexpr int32_t SETTINGS_PAGE_INDEX = 100;
        constexpr int32_t ABOUT_PAGE_INDEX = 101;
    }

    MainWindow::MainWindow()
    {
        diag::LogStartup("MainWindow: ctor begin");

        // Route UI diagnostics to the startup log. This is a debug aid: the UI cannot be
        // exercised without a desktop session, so when a control renders wrongly the only
        // way to find out why is to have it report its own state.
        ui::diagnostics::SetSink([](std::string_view message) { diag::LogStartup(std::string{message}.c_str()); });

        Title(L"TaskManagerPlusPlus");

        // --- Settings and paths ------------------------------------------------
        std::string processRulesFile;
        auto const paths = core::PathService::Create(APPLICATION_NAME, SETTINGS_FILE_NAME);
        bool verification = false;
#if defined(_DEBUG)
        verification = ui::test::WindowVerificationEnabled();
        if (verification)
        {
            m_loadedSettings.theme = core::ThemeMode::Light;
            m_loadedSettings.language = core::LanguageMode::English;
            m_loadedSettings.processSidebarVisible = false;
            m_loadedSettings.micaBackdrop = false;
        }
#endif
        if (paths.Success() && !verification)
        {
            core::SettingsStore store{paths.Value().SettingsFile()};
            m_loadedSettings = store.Load();
            m_settingsStore = std::make_unique<core::SettingsStore>(paths.Value().SettingsFile());
            processRulesFile = paths.Value().SettingsPath(PROCESS_RULES_FILE_NAME);
            if (store.DamagedFilePreserved())
            {
                diag::LogStartup("MainWindow: settings file was damaged and preserved as .bak");
            }
        }
        else
        {
            diag::LogStartup("MainWindow: could not resolve storage paths; using defaults");
        }

        core::Settings const& settings = m_loadedSettings;
        core::localization::Initialize(
            paths.Success() ? paths.Value().ApplicationDirectory() + "assets/locales" : "assets/locales",
            settings.language);

        // The session's working copy. Save writes the whole document, so every change is made to one
        // copy that is current rather than being layered over the loaded one at close.
        m_currentSettings = m_loadedSettings;

        diag::LogStartup("MainWindow: settings loaded");

        // --- Sampling ----------------------------------------------------------
        // The rules file is resolved here rather than inside the coordinator because path resolution
        // belongs to the application, and because the coordinator is then usable without one -- which
        // is what a test does so it cannot touch the user's configuration.
        m_coordinator = std::make_unique<core::SamplingCoordinator>(settings.intervalMs, processRulesFile);
        m_coordinator->ConfigureSampling(settings.intervalMs, settings.HistorySeconds());
        diag::LogStartup("MainWindow: coordinator constructed");

        // The shell is built before the pages, because selecting a navigation item
        // during construction needs the content host to already exist. Selecting a page
        // while the views are still null is handled by _selectPage.
        ui::controls::SetTheme(settings.theme == core::ThemeMode::Light ? winrt::Microsoft::UI::Xaml::ElementTheme::Light :
            settings.theme == core::ThemeMode::Dark ? winrt::Microsoft::UI::Xaml::ElementTheme::Dark : winrt::Microsoft::UI::Xaml::ElementTheme::Default);
        _buildContent();
        diag::LogStartup("MainWindow: content built");

        _createPages();
        diag::LogStartup("MainWindow: pages created");

        // Sampling starts only after the UI exists, so the first callback cannot
        // publish into a half-built window.
        if (!verification)
        {
            m_coordinator->Start();
            diag::LogStartup("MainWindow: sampling started");
            _startRefreshTimer();
        }

        // Remember the window's size and position, and everything else the session changed, for the
        // next launch. Recorded on close rather than continuously, so a crash cannot leave a
        // half-written file behind.
        Closed([this](winrt::Windows::Foundation::IInspectable const&,
                      winrt::Microsoft::UI::Xaml::WindowEventArgs const&) {
            _saveSettingsOnClose();
        });

        // Restore the remembered window size and position. A position of -1 means the
        // window has not been moved yet, which is why it is stored signed.
        if (settings.windowWidth > 0 && settings.windowHeight > 0)
        {
            if (auto const appWindow = this->AppWindow())
            {
                appWindow.Resize(winrt::Windows::Graphics::SizeInt32{settings.windowWidth, settings.windowHeight});

                if (settings.windowX >= 0 && settings.windowY >= 0)
                {
                    // The remembered position is only applied if the window would still be
                    // visible. A position saved on a larger display, or on a second monitor
                    // since removed, would otherwise place the window entirely off-screen --
                    // which presents to the user as the application failing to start at all.
                    // A saved y of 908 against a 768 pixel display was exactly that case.
                    if (_isPositionVisible(settings.windowX, settings.windowY, settings.windowWidth, settings.windowHeight))
                    {
                        winrt::Windows::Graphics::PointInt32 const position{settings.windowX, settings.windowY};
                        appWindow.Move(position);
                        diag::LogStartup("MainWindow: window position restored");
                    }
                    else
                    {
                        diag::LogStartup("MainWindow: saved window position is off-screen; using the default");
                    }
                }
            }
        }

        diag::LogStartup("MainWindow: ctor end");
    }

    void MainWindow::_createPages()
    {
        m_processesView = std::make_unique<ui::ProcessesView>(*m_coordinator);

        // Ending a process goes through the coordinator, which owns both the action and the snapshot
        // the tree is derived from.
        m_processesView->SetTerminateHandler(
            [this](uint32_t pid, bool entireTree) { return m_coordinator->TerminateProcess(pid, entireTree); });

        // The process list's column widths, remembered across sessions. The view validates each width
        // against its own column's bounds and falls back to the default for anything it rejects, so a
        // hand-edited file cannot leave a column unreadable.
        m_processesView->SetColumnWidths(m_currentSettings.processColumnWidths);
        m_processesView->SetColumnOrder(m_currentSettings.processColumnOrder);
        m_processesView->SetColumnOrderHandler([this](std::vector<uint32_t> order) {
            m_currentSettings.processColumnOrder = std::move(order);
        });
        m_processesView->SetColumnVisibility(m_currentSettings.processColumnVisible);
        m_processesView->SetColumnVisibilityHandler(
            [this](std::vector<bool> visible) { m_currentSettings.processColumnVisible = std::move(visible); });

        // A drag reports the whole set on release, and it is stored in the session's copy so the file is
        // written once on close rather than on every drag. The application owns the file and decides when
        // to save, which is why the view reports rather than writing.
        m_processesView->SetColumnWidthHandler([this](std::vector<double> widths) {
            m_currentSettings.processColumnWidths = std::move(widths);
        });

        // The detail sidebar's width and whether it is shown, remembered across sessions. Both are
        // reported together after a drag or a toggle, and held in the session's copy of the settings so
        // the file is written once on close rather than on every pixel of a drag.
        m_processesView->SetSidebarLayout(m_currentSettings.processSidebarWidth,
                                          m_currentSettings.processSidebarVisible);
        m_processesView->SetSidebarLayoutHandler([this](double width, bool visible) {
            m_currentSettings.processSidebarWidth = width;
            m_currentSettings.processSidebarVisible = visible;
        });
        // The performance view owns its sidebar width for dragging, but the application owns the
        // settings file, so the width is reported back here to be remembered.
        m_performanceView = std::make_unique<ui::PerformanceView>(
            *m_coordinator,
            m_currentSettings,
            [this](double width) { m_currentSettings.performanceSidebarWidth = width; });

        m_performanceView->SetKernelTimesHandler([this](bool show) {
            m_currentSettings.showCpuKernelTimes = show;
            m_performanceView->ApplySettings(m_currentSettings);
        });

        // The search box lives in the title bar, which this window owns, so its text is handed to the page
        // that filters on it.
        SetSearchHandler([this](std::string text) {
            if (m_processesView != nullptr)
            {
                m_processesView->SetFilter(std::move(text));
            }
        });

        // Double-clicking the sidebar asks for the compact window, and the compact view is left the same
        // way. The view reports the gesture; resizing the window is the window's own business.
        m_performanceView->SetMiniModeHandler([this]() { _setMiniMode(!m_miniMode); });
        // The page chosen in the settings, which _createPages resolved before the views existed.
        _selectPage(m_startupPageIndex);
    }

    void MainWindow::_selectPage(int32_t index)
    {
        if (index != 0 && index != 1 && index != SETTINGS_PAGE_INDEX && index != ABOUT_PAGE_INDEX) index = 0;
        // Selecting a navigation item during shell construction arrives before the
        // pages exist, so it is ignored; _createPages selects the default page once the
        // views are ready.
        if (m_contentHost == nullptr || m_processesView == nullptr || m_performanceView == nullptr)
        {
            return;
        }

        m_contentHost.Children().Clear();

        if (m_unifiedTitleBar != nullptr) {
            switch (index)
            {
                case SETTINGS_PAGE_INDEX:
                    m_pageTitle.Text(ui::Localize(L"Settings"));
                    break;
                case ABOUT_PAGE_INDEX:
                    m_pageTitle.Text(ui::Localize(L"About"));
                    break;
                case 0:
                    m_pageTitle.Text(ui::Localize(L"Processes"));
                    m_currentPage = core::StartupPage::Processes;
                    break;
                case 1:
                    m_pageTitle.Text(ui::Localize(L"Performance"));
                    m_currentPage = core::StartupPage::Performance;
                    break;
                default:
                    m_pageTitle.Text(ui::Localize(L"Processes"));
                    m_currentPage = core::StartupPage::Processes;
                    break;
            }
        }

        // The search box belongs to the process list, so it is shown only while that list is.
        //
        // It filters processes and nothing else. Leaving it on the other pages offered a control that could
        // not do anything there: typing into it on the Performance page would filter a list that is not on
        // screen and give no sign that anything had happened.
        if (m_searchBox != nullptr)
        {
            m_searchBox.Visibility(index == 0 && m_unifiedTitleBar.ActualWidth() >= 500.0 && !m_miniMode
                                              ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                                              : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
        }

        // Only one page renders at a time, so only the visible page is refreshed.
        // Refreshing a hidden page would do work whose result nobody sees.
        m_activeProcessesView = nullptr;
        m_activePerformanceView = nullptr;

        switch (index)
        {
            case SETTINGS_PAGE_INDEX:
            {
                // Rebuilt on every visit so it reflects the settings in force, rather than showing
                // stale controls after a change made elsewhere.
                m_settingsPage = std::make_unique<ui::SettingsPage>(
                    m_currentSettings, [this](core::Settings const& updated) { _applySettings(updated); });
                m_contentHost.Children().Append(m_settingsPage->Root());
                break;
            }
            case ABOUT_PAGE_INDEX:
                if (m_aboutPage == nullptr) m_aboutPage = std::make_unique<ui::AboutPage>();
                m_contentHost.Children().Append(m_aboutPage->Root());
                break;
            case 0:
                m_contentHost.Children().Append(m_processesView->Root());
                m_activeProcessesView = m_processesView.get();
                break;
            case 1:
                m_contentHost.Children().Append(m_performanceView->Root());
                m_activePerformanceView = m_performanceView.get();
                break;
            default:
                break;
        }
        ui::LocalizeTree(m_contentHost);
    }

    void MainWindow::_setMiniMode(bool mini)
    {
        if (m_miniMode == mini)
        {
            return;
        }

        m_miniMode = mini;

        // The window changes size, and nothing else does: the navigation rail, the page header and the
        // status bar all belong to the full interface and are not part of the compact one.
        if (auto const appWindow = this->AppWindow())
        {
            if (mini)
            {
                m_preMiniSize = appWindow.Size();

                // A compact window is only wide enough for the sidebar and tall enough for the rows it
                // shows. It is deliberately taller than it is wide: the list is the whole interface in
                // this mode, and a short window shows a few rows and scrolls nowhere.
                constexpr int32_t MINI_WIDTH = 360;
                constexpr int32_t MINI_HEIGHT = 960;
                appWindow.Resize(winrt::Windows::Graphics::SizeInt32{MINI_WIDTH, MINI_HEIGHT});
            }
            else if (m_preMiniSize.Width > 0 && m_preMiniSize.Height > 0)
            {
                appWindow.Resize(m_preMiniSize);
            }
        }

        // The rail's pane is hidden, not the navigation view itself. The NavigationView's content is the
        // whole content column -- the pages, the header and the status bar -- so collapsing the view
        // removed everything and left an empty window, which is exactly what it did.
        if (m_navigation != nullptr)
        {
            m_navigation.IsPaneVisible(!mini);
        }

        // The rail's resize handle belongs to the pane and goes with it.
        if (m_navigationSplitter != nullptr)
        {
            _updateNavigationSplitter();
        }

        // The title bar keeps its height: the window buttons and the drag region live in it, and a
        // compact window still has to be movable and closable. Only the page header and the status bar
        // go, since neither describes anything the compact list shows.
        m_captionActions.Visibility(
            mini ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed : winrt::Microsoft::UI::Xaml::Visibility::Visible);

        if (m_bottomStack != nullptr)
        {
            m_bottomStack.Visibility(mini ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                          : winrt::Microsoft::UI::Xaml::Visibility::Visible);
        }

        if (m_performanceView != nullptr)
        {
            m_performanceView->SetMiniMode(mini);
        }
    }

    void MainWindow::_applySettings(core::Settings const& updated)
    {
        // The settings page edits its own copy, so the application's is brought into line here.
        bool const languageChanged = m_currentSettings.language != updated.language;
        bool const samplingChanged = m_currentSettings.intervalMs != updated.intervalMs ||
            m_currentSettings.HistorySeconds() != updated.HistorySeconds();
        m_currentSettings = updated;
        if (samplingChanged) m_coordinator->ConfigureSampling(updated.intervalMs, updated.HistorySeconds());
        core::localization::SetLanguage(updated.language);

        // The topmost state is applied immediately: it is the one setting whose effect is expected
        // the moment it is changed rather than at the next launch.
        if (auto const appWindow = this->AppWindow())
        {
            if (auto const presenter =
                    appWindow.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>())
            {
                presenter.IsAlwaysOnTop(updated.alwaysOnTop);
            }
        }

        // The theme is applied to the root element rather than to the application, because the root
        // is where FrameworkElement::RequestedTheme is honoured; setting it on the Application object
        // is only read once, at construction.
        if (m_rootGrid != nullptr)
        {
            switch (updated.theme)
            {
                case core::ThemeMode::Light:
                    m_rootGrid.RequestedTheme(winrt::Microsoft::UI::Xaml::ElementTheme::Light);
                    break;
                case core::ThemeMode::Dark:
                    m_rootGrid.RequestedTheme(winrt::Microsoft::UI::Xaml::ElementTheme::Dark);
                    break;
                case core::ThemeMode::System:
                default:
                    m_rootGrid.RequestedTheme(winrt::Microsoft::UI::Xaml::ElementTheme::Default);
                    break;
            }
        }

        ui::controls::SetTheme(m_rootGrid.ActualTheme());
        _applyWindowBackdrop();
        _updateTitleBarTheme();

        // The chart styles are pushed to the view so a colour change is visible without a restart.
        if (m_performanceView != nullptr)
        {
            m_performanceView->ApplySettings(updated);
        }

        if (m_rootGrid != nullptr) m_rootGrid.Language(ui::Localize(core::localization::Locale()));
        if (languageChanged && m_rootGrid != nullptr) {
            m_rootGrid.Language(ui::Localize(core::localization::Locale()));
            ui::LocalizeTree(m_rootGrid);
            _updateStatusBar();
        }
    }

    void MainWindow::_updateNavigationSplitter()
    {
        if (m_navigationSplitter == nullptr || m_navigation == nullptr)
        {
            return;
        }

        // The handle straddles the pane's right edge, so its left offset is the open length less half
        // its own width. It is hidden while the rail is collapsed, where there is no width to drag.
        double const width = m_navigation.IsPaneOpen() ? m_navigation.OpenPaneLength() : 0.0;
        m_navigationSplitter.Margin(winrt::Microsoft::UI::Xaml::ThicknessHelper::FromLengths(
            width - (ui::metrics::SPLITTER_WIDTH / 2.0), 0.0, 0.0, 0.0));
        m_navigationSplitter.Visibility(!m_miniMode && m_navigation.IsPaneVisible() && m_navigation.IsPaneOpen()
                                                                 ? winrt::Microsoft::UI::Xaml::Visibility::Visible
                                                                 : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
    }

    void MainWindow::_saveSettingsOnClose()
    {
        if (m_settingsStore == nullptr)
        {
            return;
        }

        // Starts from what the session has been updating rather than from what was loaded, so every
        // choice made since launch is kept. Saving from the loaded copy is what would silently
        // discard a page selection or a sidebar width the user changed.
        core::Settings updated = m_currentSettings;

        // The page in use, so LastUsed can resume it.
        updated.lastUsedPage = m_currentPage;

        // The rail's state, so the application reopens the way it was left.
        if (m_navigation != nullptr)
        {
            updated.navigationExpanded = m_navigation.IsPaneOpen();
            if (updated.navigationExpanded)
            {
                updated.navigationWidth = m_navigation.OpenPaneLength();
            }
        }

        // The window's placement.
        //
        // While mini mode is on, the current size is the compact window's and the position is wherever
        // that landed, so neither is the user's choice for the full window. Writing them would mean
        // closing the application from the compact view reopened it compact, which reads as the window
        // size having been lost. The mode is left before the placement is recorded instead.
        if (m_miniMode)
        {
            _setMiniMode(false);
        }

        if (auto const appWindow = this->AppWindow())
        {
            updated.windowMaximized = appWindow.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>() !=
                                          nullptr &&
                                      appWindow.Presenter().try_as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>()
                                              .State() == winrt::Microsoft::UI::Windowing::OverlappedPresenterState::Maximized;

            // The size and position are taken from the restored bounds, not the current ones: while
            // maximised the current size is the screen's, and recording that would lose the size the
            // user chose for the restored window.
            auto const size = appWindow.Size();
            auto const position = appWindow.Position();
            updated.windowWidth = size.Width;
            updated.windowHeight = size.Height;
            updated.windowX = position.X;
            updated.windowY = position.Y;
        }

        if (auto const saved = m_settingsStore->Save(updated); !saved.Success())
        {
            diag::LogStartup("MainWindow: could not save settings on close");
        }
    }

    bool MainWindow::_isPositionVisible(int32_t x, int32_t y, int32_t width, int32_t height) noexcept
    {
        // The virtual desktop spans every attached display, so its bounds are the right test
        // rather than the primary display's: a window on a second monitor is perfectly valid.
        int const virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int const virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int const virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int const virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (virtualWidth <= 0 || virtualHeight <= 0)
        {
            // The metrics are unavailable, which should not happen. Refusing to move the
            // window leaves it at the platform default, which is always visible.
            return false;
        }

        int const virtualRight = virtualLeft + virtualWidth;
        int const virtualBottom = virtualTop + virtualHeight;

        // Some of the window has to overlap the desktop, and it must be a usable part rather
        // than a sliver: a window with only its last pixel on screen cannot be grabbed.
        constexpr int32_t MIN_VISIBLE = 64;
        int32_t const visibleWidth = (std::min)(x + width, virtualRight) - (std::max)(x, virtualLeft);
        int32_t const visibleHeight = (std::min)(y + height, virtualBottom) - (std::max)(y, virtualTop);

        return visibleWidth >= MIN_VISIBLE && visibleHeight >= MIN_VISIBLE;
    }

    void MainWindow::_buildContent()
    {
        // --- Navigation shell ---------------------------------------------------
        m_navigation = NavigationView();
        m_navigation.IsSettingsVisible(false);
        m_navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        // LeftCompact rather than Left, which matters: with Left the pane is always expanded and
        // IsPaneOpen is ignored, so the rail could never be collapsed. LeftCompact shows the narrow
        // icon strip when the pane is closed and the full rail when it is open, which is what the
        // toggle and the persisted state both need.
        m_navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::LeftCompact);
        m_navigation.OpenPaneLength(ui::metrics::NAVIGATION_PANE_WIDTH);
        m_navigation.IsTitleBarAutoPaddingEnabled(false);

        auto addItem = [this](wchar_t const* label, wchar_t const* glyph) {
            NavigationViewItem item;
            item.Content(winrt::box_value(ui::Localize(label)));
            FontIcon icon;
            icon.Glyph(glyph);
            item.Icon(icon);
            m_navigation.MenuItems().Append(item);
            return item;
        };

        // Segoe Fluent Icons glyphs.
        addItem(L"Processes", L"\xE9D9");
        addItem(L"Performance", L"\xE9D2");

        // Settings goes in the footer, which is where the original puts it: a separate list, so the
        // selection handler can tell it apart from the pages above.
        {
            NavigationViewItem settingsItem;
            settingsItem.Content(winrt::box_value(ui::Localize(L"Settings")));
            FontIcon settingsIcon;
            settingsIcon.Glyph(L"\xE713");
            settingsItem.Icon(settingsIcon);
            m_navigation.FooterMenuItems().Append(settingsItem);
            NavigationViewItem aboutItem;
            aboutItem.Content(winrt::box_value(ui::Localize(L"About")));
            FontIcon aboutIcon;
            aboutIcon.Glyph(L"\xE946");
            aboutItem.Icon(aboutIcon);
            m_navigation.FooterMenuItems().Append(aboutItem);
        }

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

                // IVector::IndexOf reports the position through an out parameter and returns a bool,
                // rather than returning the index directly. The footer is checked first: a
                // NavigationView reports its selection through FooterMenuItems for those entries, and
                // looking only at MenuItems would treat Settings as no selection at all.
                uint32_t index = 0;
                if (view.FooterMenuItems().IndexOf(selected, index))
                {
                    _selectPage(index == 1 ? ABOUT_PAGE_INDEX : SETTINGS_PAGE_INDEX);
                    return;
                }

                if (view.MenuItems().IndexOf(selected, index))
                {
                    _selectPage(static_cast<int32_t>(index));
                }
            });

        // The page to open on. LastUsed resumes whatever was in use at the last close, which the
        // settings file carries separately so the two cannot contradict each other.
        core::StartupPage const startupPage = (m_currentSettings.startupPage == core::StartupPage::LastUsed)
                                                  ? m_currentSettings.lastUsedPage
                                                  : m_currentSettings.startupPage;

        int32_t const startupIndex = startupPage == core::StartupPage::Performance ? 1 : 0;
        m_startupPageIndex = startupIndex;

        m_navigation.SelectedItem(m_navigation.MenuItems().GetAt(static_cast<uint32_t>(startupIndex)));

        // The rail's remembered state. A user who collapsed it to gain horizontal space should not
        // have to collapse it again on every launch.
        m_navigation.IsPaneOpen(m_currentSettings.navigationExpanded);
        if (m_currentSettings.navigationExpanded && m_currentSettings.navigationWidth > 0.0)
        {
            m_navigation.OpenPaneLength(m_currentSettings.navigationWidth);
        }

        // --- Status bar ---------------------------------------------------------
        Grid statusBar = Grid();
        statusBar.Height(ui::metrics::STATUS_BAR_HEIGHT);
        statusBar.Padding(
            ThicknessHelper::FromLengths(ui::metrics::PAGE_MARGIN, 0.0, ui::metrics::PAGE_MARGIN, 0.0));
        statusBar.Background(ui::controls::ThemedBrush(ui::theme::LAYER_BACKGROUND));

        m_statusText = ui::controls::MakeText(L"", 12.0, true);
        m_statusText.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        m_statusText.TextTrimming(winrt::Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        m_statusText.VerticalAlignment(VerticalAlignment::Center);
        statusBar.Children().Append(m_statusText);

        // --- Permission notice --------------------------------------------------
        // Shown only when a capability is missing, so a fully privileged run shows
        // nothing. A modal dialog would be wrong here: the application still works, it
        // just shows less.
        m_permissionBar = InfoBar();
        m_permissionBar.Severity(InfoBarSeverity::Informational);
        m_permissionBar.Title(L"Limited data");
        m_permissionBar.Message(
            L"Some metrics are unavailable at the current privilege level. "
            L"I/O counters and process owners need elevation.");
        m_permissionBar.IsOpen(false);
        m_permissionBar.IsClosable(true);

        // --- Unified title bar -------------------------------------------------
        // Native caption insets and non-client regions leave the actions next to the system buttons.
        m_unifiedTitleBar = Grid();
        m_unifiedTitleBar.Height(APP_TITLE_BAR_HEIGHT);
        m_unifiedTitleBar.ColumnDefinitions().Append(ui::controls::MakeAutoColumn());
        m_unifiedTitleBar.ColumnDefinitions().Append(ui::controls::MakeStarColumn());
        m_unifiedTitleBar.ColumnDefinitions().Append(ui::controls::MakeAutoColumn());
        m_captionButtonInset = ui::controls::MakeFixedColumn(138.0);
        m_unifiedTitleBar.ColumnDefinitions().Append(m_captionButtonInset);
        auto identity = ui::controls::MakeRow(12.0);
        identity.Margin(ThicknessHelper::FromLengths(ui::metrics::CONTENT_LEFT_INSET, 0.0, 16.0, 0.0));
        FontIcon appIcon;
        appIcon.Glyph(L"\xE9D9");
        appIcon.FontSize(18.0);
        identity.Children().Append(appIcon);
        m_brandName = ui::controls::MakeText(L"Task Manager++", 13.0);
        m_brandName.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        identity.Children().Append(m_brandName);
        winrt::Microsoft::UI::Xaml::Controls::Border divider;
        divider.Width(1.0);
        divider.Height(18.0);
        divider.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush({0x60, 0x88, 0x88, 0x88}));
        identity.Children().Append(divider);
        m_pageTitle = ui::controls::MakeHeading(L"Processes", 13.0);
        m_pageTitle.TextWrapping(winrt::Microsoft::UI::Xaml::TextWrapping::NoWrap);
        identity.Children().Append(m_pageTitle);
        m_unifiedTitleBar.Children().Append(identity);

        m_searchBox = winrt::Microsoft::UI::Xaml::Controls::TextBox();
        m_searchBox.PlaceholderText(ui::Localize(L"Search processes (name or PID)"));
        m_searchBox.Height(32.0);
        m_searchBox.MinWidth(0.0);
        m_searchBox.MaxWidth(ui::metrics::SEARCH_MAX_WIDTH);
        m_searchBox.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 16.0, 0.0));
        m_searchBox.VerticalAlignment(VerticalAlignment::Center);
        m_searchBox.TextChanged([this](auto const&, auto const&) {
            if (m_onSearch) m_onSearch(winrt::to_string(m_searchBox.Text()));
        });
        m_searchBox.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Stretch);
        Grid::SetColumn(m_searchBox, 1);
        m_unifiedTitleBar.Children().Append(m_searchBox);

        StackPanel actions = ui::controls::MakeRow(4.0);
        m_captionActions = actions;
        actions.VerticalAlignment(VerticalAlignment::Center);
        winrt::Microsoft::UI::Xaml::Controls::Button runTask;
        runTask.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        runTask.BorderThickness(ThicknessHelper::FromUniformLength(0.0));
        runTask.Padding(ThicknessHelper::FromLengths(10.0, 6.0, 10.0, 6.0));
        StackPanel runContent = ui::controls::MakeRow(6.0);
        FontIcon runIcon;
        runIcon.Glyph(L"\xE8A7");
        runIcon.FontSize(14.0);
        runContent.Children().Append(runIcon);
        auto runLabel = ui::controls::MakeText(L"Run new task", 12.0);
        runContent.Children().Append(runLabel);
        runTask.Content(runContent);
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            runTask, winrt::box_value(ui::Localize(L"Run new task")));
        runTask.Click([this](auto const&, auto const&) { _showRunTaskDialog(); });
        actions.Children().Append(runTask);

        winrt::Microsoft::UI::Xaml::Controls::Button overflow;
        overflow.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        overflow.BorderThickness(ThicknessHelper::FromUniformLength(0.0));
        overflow.Padding(ThicknessHelper::FromLengths(10.0, 6.0, 10.0, 6.0));
        FontIcon moreIcon;
        moreIcon.Glyph(L"\xE712");
        moreIcon.FontSize(14.0);
        overflow.Content(moreIcon);
        winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(
            overflow, winrt::box_value(ui::Localize(L"More options")));
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyout menu;
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem refresh;
        refresh.Text(ui::Localize(L"Refresh now"));
        refresh.Click([this](auto const&, auto const&) { m_coordinator->RequestRefresh(); });
        menu.Items().Append(refresh);
        winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutItem settings;
        settings.Text(ui::Localize(L"Settings"));
        settings.Click(
            [this](auto const&, auto const&) { m_navigation.SelectedItem(m_navigation.FooterMenuItems().GetAt(0)); });
        menu.Items().Append(settings);
        overflow.Flyout(menu);
        actions.Children().Append(overflow);
        Grid::SetColumn(actions, 2);
        m_unifiedTitleBar.Children().Append(actions);
        m_unifiedTitleBar.SizeChanged(
            [this, runLabel, identity, divider](auto const&, winrt::Microsoft::UI::Xaml::SizeChangedEventArgs const& args) {
                double const width = args.NewSize().Width;
                runLabel.Visibility(width < 760.0 ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                                  : winrt::Microsoft::UI::Xaml::Visibility::Visible);
                m_brandName.Visibility(width < 640.0 ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed
                                                     : winrt::Microsoft::UI::Xaml::Visibility::Visible);
                identity.Visibility(width < 340.0 ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed : winrt::Microsoft::UI::Xaml::Visibility::Visible);
                divider.Visibility(width < 640.0 ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed : winrt::Microsoft::UI::Xaml::Visibility::Visible);
                m_pageTitle.Visibility(width < 400.0 ? winrt::Microsoft::UI::Xaml::Visibility::Collapsed : winrt::Microsoft::UI::Xaml::Visibility::Visible);
                m_searchBox.Visibility(width >= 500.0 && m_activeProcessesView != nullptr ? winrt::Microsoft::UI::Xaml::Visibility::Visible : winrt::Microsoft::UI::Xaml::Visibility::Collapsed);
            });

        // --- Assembly -----------------------------------------------------------
        //
        Grid contentColumn = Grid();
        m_titleBarRow = ui::controls::MakeFixedRow(APP_TITLE_BAR_HEIGHT);
        contentColumn.RowDefinitions().Append(m_titleBarRow);
        contentColumn.RowDefinitions().Append(ui::controls::MakeStarRow());
        m_bottomRow = ui::controls::MakeAutoRow();
        contentColumn.RowDefinitions().Append(m_bottomRow);
        m_titleBarSpacer = Grid();
        m_titleBarSpacer.Children().Append(m_unifiedTitleBar);
        Grid::SetRow(m_titleBarSpacer, 0);
        contentColumn.Children().Append(m_titleBarSpacer);

        m_contentHost = Grid();
        Grid::SetRow(m_contentHost, 1);
        contentColumn.Children().Append(m_contentHost);

        // The permission bar and the status bar share the last row, the notice above
        // the status line, so neither shifts the page when it appears.
        //
        // The stack is a member because mini mode hides the whole of it. It must be built once and its
        // children appended once: adding the same element to two parents raises "Element is already the
        // child of another element", which is what an earlier version of this did.
        m_bottomStack = Grid();
        m_bottomStack.RowDefinitions().Append(ui::controls::MakeAutoRow());
        m_bottomStack.RowDefinitions().Append(ui::controls::MakeAutoRow());
        Grid::SetRow(m_permissionBar, 0);
        m_bottomStack.Children().Append(m_permissionBar);
        Grid::SetRow(statusBar, 1);
        m_bottomStack.Children().Append(statusBar);

        Grid::SetRow(m_bottomStack, 2);
        contentColumn.Children().Append(m_bottomStack);
        m_navigation.Content(contentColumn);

        m_rootGrid = Grid();
        m_rootGrid.Background(ui::controls::ThemedBrush(ui::theme::PAGE_BACKGROUND));
        m_rootGrid.Children().Append(m_navigation);
        ui::controls::ConfigureThemeResources(m_rootGrid);

        // --- Navigation rail resize handle -------------------------------------
        //
        // A NavigationView has no splitter, so the handle is a narrow transparent strip overlaid on
        // the pane's right edge. It is a sibling of the navigation view rather than a child because
        // the pane's own content area clips what it contains, and the strip has to straddle the edge
        // to be grabbable from either side.
        //
        // A transparent brush rather than none: a null Background is not hit-testable in WinUI, so a
        // handle without one would never receive the pointer.
        m_navigationSplitter = winrt::Microsoft::UI::Xaml::Controls::Border();
        m_navigationSplitter.Width(ui::metrics::SPLITTER_WIDTH);
        m_navigationSplitter.HorizontalAlignment(winrt::Microsoft::UI::Xaml::HorizontalAlignment::Left);
        m_navigationSplitter.VerticalAlignment(winrt::Microsoft::UI::Xaml::VerticalAlignment::Stretch);
        m_navigationSplitter.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
        _updateNavigationSplitter();

        m_navigationSplitter.PointerEntered(
            [this](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
                // Only offer the affordance when the rail is open: a collapsed rail has no width to
                // drag, and showing a resize cursor over the icon strip would be misleading.
                if (m_navigation.IsPaneOpen())
                {
                    m_navigationSplitter.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
                        winrt::Windows::UI::Color{0x40, 0x80, 0x80, 0x80}));
                }
            });
        m_navigationSplitter.PointerExited(
            [this](winrt::Windows::Foundation::IInspectable const&,
                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
                m_navigationSplitter.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            });

        // The drag is measured as movement from where it began rather than from the absolute pointer
        // position, so the boundary stays under the cursor wherever the drag starts. The position is read
        // in the window's frame: the rail's edge -- and so the handle on it -- moves as the pane widens,
        // and a position measured from the handle would be measured from a frame that moves with the drag.
        auto const dragStartWidth = std::make_shared<double>(0.0);
        auto const dragStartX = std::make_shared<double>(0.0);

        m_navigationSplitter.PointerPressed(
            [this, dragStartWidth, dragStartX](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
                auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
                if (element == nullptr)
                {
                    return;
                }

                // GetCurrentPoint returns null when the pointer has no position relative to the element,
                // which happens as it leaves or the element is removed mid-gesture; calling Position() on
                // it dereferences null.
                auto const point = args.GetCurrentPoint(nullptr);
                if (point == nullptr)
                {
                    return;
                }

                *dragStartWidth = m_navigation.OpenPaneLength();
                *dragStartX = point.Position().X;
                element.CapturePointer(args.Pointer());
            });

        m_navigationSplitter.PointerMoved(
            [this, dragStartWidth, dragStartX](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
                auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
                if (element == nullptr)
                {
                    return;
                }

                // PointerCaptures() is only non-null once the pointer system has given the element a
                // capture collection; calling Size() before that dereferences null.
                auto const captures = element.PointerCaptures();
                if (captures == nullptr || captures.Size() == 0)
                {
                    return;
                }

                auto const point = args.GetCurrentPoint(nullptr);
                if (point == nullptr)
                {
                    return;
                }

                // Clamped between a width that still shows the labels and one that leaves the content
                // usable. A rail narrower than its longest label is unreadable, and one wider than
                // this crowds the page it exists to navigate.
                constexpr double MIN_RAIL = 180.0;
                constexpr double MAX_RAIL = 420.0;

                double const width = ui::DraggedWidth(*dragStartWidth, *dragStartX, point.Position().X,
                                                      MIN_RAIL, MAX_RAIL);

                m_navigation.OpenPaneLength(width);
                m_currentSettings.navigationWidth = width;
                _updateNavigationSplitter();
            });

        // The toggle changes the pane's state without going through the drag path, so the handle is
        // realigned whenever it opens or closes.
        m_navigation.PaneOpening([this](winrt::Microsoft::UI::Xaml::Controls::NavigationView const&,
                                        winrt::Windows::Foundation::IInspectable const&) {
            _updateNavigationSplitter();
        });
        m_navigation.PaneClosing([this](winrt::Microsoft::UI::Xaml::Controls::NavigationView const&,
                                        winrt::Microsoft::UI::Xaml::Controls::NavigationViewPaneClosingEventArgs const&) {
            _updateNavigationSplitter();
        });

        m_navigationSplitter.PointerReleased(
            [this](winrt::Windows::Foundation::IInspectable const& sender,
                   winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
                auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
                if (element == nullptr)
                {
                    return;
                }
                element.ReleasePointerCapture(args.Pointer());

                // The width is recorded as it is dragged, so there is nothing to commit here. The
                // settings are written when the window closes.
                m_navigationSplitter.Background(
            winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(winrt::Windows::UI::Colors::Transparent()));
            });

        m_rootGrid.Children().Append(m_navigationSplitter);

        Content(m_rootGrid);
        m_rootGrid.ActualThemeChanged([this](auto const&, auto const&) { _updateTitleBarTheme(); });

        // Keep system caption buttons and use native hit testing for the title strip and rail.
        ExtendsContentIntoTitleBar(true);
        // Declare caption and interactive rectangles through the official non-client input API.
        m_rootGrid.LayoutUpdated([this](auto const&, auto const&) { _updateNativeDragRegions(); });

        // Align caption buttons to the same 48px row as the app's controls.
        AppWindow().TitleBar().PreferredHeightOption(winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Tall);

        m_windowHandle = winrt::Microsoft::UI::GetWindowFromWindowId(AppWindow().Id());

        // Mini mode also permits dragging the graph body. The full shell uses native caption regions.
        ui::controls::MakeWindowDragRegion(
            m_rootGrid,
            m_windowHandle,
            [this](winrt::Windows::Foundation::Point const&) { return m_miniMode; },
            /*includeHandled=*/true);

        // The remembered theme and the chart styles, applied once the root exists. Doing this only
        // from the settings page would mean a choice took effect only after visiting that page.
        _applySettings(m_currentSettings);
        _updateTitleBarTheme();

        _updateStatusBar();
    }

    void MainWindow::_updateNativeDragRegions()
    {
        using namespace winrt::Microsoft::UI::Xaml;
        using winrt::Windows::Graphics::RectInt32;
        if (m_rootGrid.XamlRoot() == nullptr || m_rootGrid.ActualWidth() <= 0.0) return;
        try
        {
            double const scale = m_rootGrid.XamlRoot().RasterizationScale();
            int32_t const width = static_cast<int32_t>(std::round(m_rootGrid.ActualWidth() * scale));
            int32_t const height = static_cast<int32_t>(std::round(m_rootGrid.ActualHeight() * scale));
            int32_t const inset = AppWindow().TitleBar().RightInset();
            double const logicalInset = static_cast<double>(inset) / scale;
            if (std::abs(m_captionButtonInset.Width().Value - logicalInset) > 0.01)
                m_captionButtonInset.Width(GridLengthHelper::FromPixels(logicalInset));
            int32_t const captionHeight = static_cast<int32_t>(std::round(APP_TITLE_BAR_HEIGHT * scale));
            std::vector<RectInt32> captions{{0, 0, (std::max)(0, width - inset), captionHeight}};
            if (!m_miniMode && m_navigation.IsPaneVisible())
            {
                double const rail = m_navigation.IsPaneOpen() ? m_navigation.OpenPaneLength() : m_navigation.CompactPaneLength();
                captions.push_back({0, captionHeight, static_cast<int32_t>(std::round(rail * scale)),
                    (std::max)(0, height - captionHeight)});
            }
            std::vector<RectInt32> interactive;
            auto add = [this, scale, width, height, &interactive](FrameworkElement const& element) {
                if (element == nullptr || element.Visibility() != Visibility::Visible || element.ActualWidth() <= 0.0 || element.ActualHeight() <= 0.0) return;
                auto const bounds = element.TransformToVisual(m_rootGrid).TransformBounds({0.0f, 0.0f,
                    static_cast<float>(element.ActualWidth()), static_cast<float>(element.ActualHeight())});
                int32_t const x = (std::max)(0, static_cast<int32_t>(std::floor(bounds.X * scale)));
                int32_t const y = (std::max)(0, static_cast<int32_t>(std::floor(bounds.Y * scale)));
                int32_t const right = (std::min)(width, static_cast<int32_t>(std::ceil((bounds.X + bounds.Width) * scale)));
                int32_t const bottom = (std::min)(height, static_cast<int32_t>(std::ceil((bounds.Y + bounds.Height) * scale)));
                if (right > x && bottom > y) interactive.push_back({x, y, right - x, bottom - y});
            };
            add(m_searchBox);
            if (m_captionActions.Visibility() == Visibility::Visible)
                for (auto const& child : m_captionActions.Children()) add(child.as<FrameworkElement>());
            if (!m_miniMode && m_navigation.IsPaneVisible())
            {
                for (auto const& item : m_navigation.MenuItems()) add(item.as<FrameworkElement>());
                for (auto const& item : m_navigation.FooterMenuItems()) add(item.as<FrameworkElement>());
                add(m_navigationSplitter);
                std::vector<DependencyObject> pending{m_navigation};
                while (!pending.empty())
                {
                    auto current = pending.back();
                    pending.pop_back();
                    if (auto const element = current.try_as<FrameworkElement>(); element != nullptr && element.Name() == L"TogglePaneButton") add(element);
                    if (auto const scrollbar = current.try_as<winrt::Microsoft::UI::Xaml::Controls::Primitives::ScrollBar>()) add(scrollbar);
                    int const count = Media::VisualTreeHelper::GetChildrenCount(current);
                    // Navigation content contains the process table; the caption rail only needs its own template.
                    if (current == m_contentHost) continue;
                    for (int i = 0; i < count; ++i) pending.push_back(Media::VisualTreeHelper::GetChild(current, i));
                }
            }
            // Non-client regions cannot overlap: a later passthrough rectangle replaces intersecting
            // caption rectangles. Split the caption around controls before registering either kind.
            for (auto const& control : interactive)
            {
                std::vector<RectInt32> remaining;
                for (auto const& rect : captions)
                {
                    int32_t const left = (std::max)(rect.X, control.X);
                    int32_t const top = (std::max)(rect.Y, control.Y);
                    int32_t const right = (std::min)(rect.X + rect.Width, control.X + control.Width);
                    int32_t const bottom = (std::min)(rect.Y + rect.Height, control.Y + control.Height);
                    if (right <= left || bottom <= top) { remaining.push_back(rect); continue; }
                    if (top > rect.Y) remaining.push_back({rect.X, rect.Y, rect.Width, top - rect.Y});
                    if (bottom < rect.Y + rect.Height) remaining.push_back({rect.X, bottom, rect.Width, rect.Y + rect.Height - bottom});
                    if (left > rect.X) remaining.push_back({rect.X, top, left - rect.X, bottom - top});
                    if (right < rect.X + rect.Width) remaining.push_back({right, top, rect.X + rect.Width - right, bottom - top});
                }
                captions = std::move(remaining);
            }
            auto same = [](auto const& a, auto const& b) {
                return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](auto const& x, auto const& y) {
                    return x.X == y.X && x.Y == y.Y && x.Width == y.Width && x.Height == y.Height;
                });
            };
            if (m_nonClientPointerSource == nullptr)
                m_nonClientPointerSource = winrt::Microsoft::UI::Input::InputNonClientPointerSource::GetForWindowId(AppWindow().Id());
            if (!same(interactive, m_passthroughRects))
            {
                m_nonClientPointerSource.SetRegionRects(winrt::Microsoft::UI::Input::NonClientRegionKind::Passthrough, interactive);
                m_passthroughRects = std::move(interactive);
            }
            if (!same(captions, m_captionRects))
            {
                m_nonClientPointerSource.SetRegionRects(winrt::Microsoft::UI::Input::NonClientRegionKind::Caption, captions);
                m_captionRects = std::move(captions);
            }
        }
        catch (winrt::hresult_error const&)
        {
            // Layout can be transiently disconnected while the window or navigation pane closes.
        }
    }

    void MainWindow::_applyWindowBackdrop()
    {
        using namespace winrt::Microsoft::UI::Xaml::Media;
        auto transparent = SolidColorBrush(winrt::Windows::UI::Colors::Transparent());
        if (m_currentSettings.micaBackdrop)
        {
            if (SystemBackdrop() == nullptr) SystemBackdrop(MicaBackdrop());
            m_rootGrid.Background(transparent);
        }
        else
        {
            SystemBackdrop(nullptr);
            m_rootGrid.Background(ui::controls::ThemedBrush(ui::theme::PAGE_BACKGROUND));
        }
        // Content can show the window material, but the overlay pane must cover the page beneath it.
        m_navigation.Resources().Insert(winrt::box_value(L"NavigationViewContentBackground"),
            m_currentSettings.micaBackdrop ? transparent : ui::controls::ThemedBrush(ui::theme::LAYER_BACKGROUND));
        for (auto key : {L"NavigationViewDefaultPaneBackground", L"NavigationViewExpandedPaneBackground", L"NavigationViewTopPaneBackground"})
            m_navigation.Resources().Insert(winrt::box_value(key), ui::controls::ThemedBrush(ui::theme::PAGE_BACKGROUND));
    }

    void MainWindow::_updateTitleBarTheme()
    {
        ui::controls::SetTheme(m_rootGrid.ActualTheme());
        bool const dark = m_rootGrid.ActualTheme() == winrt::Microsoft::UI::Xaml::ElementTheme::Dark;
        auto primary = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(dark
                ? winrt::Windows::UI::Color{0xFF, 0xF3, 0xF3, 0xF3}
                : winrt::Windows::UI::Color{0xFF, 0x20, 0x20, 0x20});
        auto secondary = winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(dark
                ? winrt::Windows::UI::Color{0xFF, 0xCB, 0xCB, 0xCB}
                : winrt::Windows::UI::Color{0xFF, 0x55, 0x55, 0x55});
        auto caption = AppWindow().TitleBar();
        caption.ButtonForegroundColor(primary.Color());
        caption.ButtonInactiveForegroundColor(secondary.Color());
        caption.ButtonBackgroundColor(winrt::Windows::UI::Colors::Transparent());
        caption.ButtonInactiveBackgroundColor(winrt::Windows::UI::Colors::Transparent());
        caption.ButtonHoverForegroundColor(primary.Color());
        caption.ButtonHoverBackgroundColor(dark ? winrt::Windows::UI::Color{0x20, 0xFF, 0xFF, 0xFF} : winrt::Windows::UI::Color{0x10, 0x00, 0x00, 0x00});
        m_brandName.Foreground(primary);
        m_pageTitle.Foreground(primary);
        m_searchBox.Foreground(primary);
        for (auto key : {L"TextControlPlaceholderForeground",
                 L"TextControlPlaceholderForegroundFocused",
                 L"TextControlPlaceholderForegroundPointerOver"})
            m_searchBox.Resources().Insert(winrt::box_value(key), secondary);
    }

    void MainWindow::_showRunTaskDialog()
    {
        try
        {
            // Ask Windows Shell to open its native Run dialog, including shell command handling
            // and command history, through the documented Shell.Application automation API.
            auto const shell = winrt::create_instance<IShellDispatch>(__uuidof(Shell),
                CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER);
            winrt::check_hresult(shell->FileRun());
        }
        catch (winrt::hresult_error const& error)
        {
            diag::LogStartup(winrt::to_string(error.message()).c_str());
        }
    }

    void MainWindow::_startRefreshTimer()
    {
        auto const queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_refreshTimer = queue.CreateTimer();
        m_refreshTimer.Interval(UI_REFRESH_INTERVAL);
        m_refreshTimer.IsRepeating(true);
        m_refreshTimer.Tick([this](auto const&, auto const&) {
            // Measures the gap since the previous tick, which is how a stall anywhere in this loop or in
            // the layout it provokes shows up as a number rather than as "the window felt slow".
            ui::diagnostics::NoteUiTick(static_cast<double>(UI_REFRESH_INTERVAL.count()));

            ui::diagnostics::ScopedTimer const tickTimer{"ui loop: whole tick"};

            // Each page's Refresh is a no-op when nothing new was published, so this can
            // run frequently without cost.
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
        text += L"  |  ";
        text += std::to_wstring(m_coordinator->SampleCount());
        text += L" samples";

        // A run of failures is worth stating: silently showing stale numbers is the
        // worst outcome for a monitoring tool.
        if (status.consecutiveFailures > 0)
        {
            text += L"  |  ";
            text += std::to_wstring(status.consecutiveFailures);
            text += L" consecutive sampling failures";
        }

        auto const elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - m_startedAt).count();
        auto const statusText = ui::Localize(text) + L"  |  " +
            ui::Localize("Uptime " + ui::FormatDuration(static_cast<uint64_t>(elapsed)));
        if (m_statusText.Text() != statusText)
        {
            m_statusText.Text(statusText);
            winrt::Microsoft::UI::Xaml::Controls::ToolTipService::SetToolTip(m_statusText, winrt::box_value(statusText));
        }

        // The notice appears only when process I/O counts are actually unavailable.
        auto const capabilities = m_coordinator->ProcessCapabilities();
        bool const missingData = !capabilities.hasIoCounters || !capabilities.hasCpuTimes;
        m_permissionBar.IsOpen(missingData);
    }
}
