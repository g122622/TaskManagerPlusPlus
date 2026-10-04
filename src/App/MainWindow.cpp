#include "WinRT.h"

#include "MainWindow.h"
#include "StartupLog.h"

#include "Core/Logging.h"
#include "Core/PathService.h"
#include "Core/Settings.h"
#include "UI/Theming/Controls.h"
#include "UI/Diagnostics.h"
#include "UI/Theming/Formatting.h"
#include "UI/Theming/Theme.h"

#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <chrono>
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

        /// Height of the custom title bar strip.
        ///
        /// The window buttons occupy a fixed region at the top right. Reserving its
        /// height here is the equivalent of a CSS calc(): the strip is out of the flow
        /// of the content rows below it, and nothing drawn there can land under the
        /// buttons. Without this the CPU percentage was drawn on top of them.
        constexpr double TITLE_BAR_HEIGHT = 40.0;

        /// Height of the app title bar: the strip carrying "Task Manager".
        constexpr double APP_TITLE_BAR_HEIGHT = 36.0;

        /// Height of the page header bar: the strip carrying the page name and the task
        /// actions. The original shows both bars stacked at the top of the window.
        constexpr double PAGE_HEADER_HEIGHT = 48.0;

        /// Index used for the Settings page. Outside the range a navigation item can produce, so it
        /// cannot collide with a page the user selected from the main list.
        constexpr int32_t SETTINGS_PAGE_INDEX = 100;
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
        auto const paths = core::PathService::Create(APPLICATION_NAME, SETTINGS_FILE_NAME);
        if (paths.Success())
        {
            core::SettingsStore store{paths.Value().SettingsFile()};
            m_loadedSettings = store.Load();
            m_settingsStore = std::make_unique<core::SettingsStore>(paths.Value().SettingsFile());
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

        // The session's working copy. Save writes the whole document, so every change is made to one
        // copy that is current rather than being layered over the loaded one at close.
        m_currentSettings = m_loadedSettings;

        diag::LogStartup("MainWindow: settings loaded");

        // --- Sampling ----------------------------------------------------------
        m_coordinator = std::make_unique<core::SamplingCoordinator>(settings.intervalMs);
        diag::LogStartup("MainWindow: coordinator constructed");

        // The shell is built before the pages, because selecting a navigation item
        // during construction needs the content host to already exist. Selecting a page
        // while the views are still null is handled by _selectPage.
        _buildContent();
        diag::LogStartup("MainWindow: content built");

        _createPages();
        diag::LogStartup("MainWindow: pages created");

        // Sampling starts only after the UI exists, so the first callback cannot
        // publish into a half-built window.
        m_coordinator->Start();
        diag::LogStartup("MainWindow: sampling started");

        _startRefreshTimer();

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
        // The performance view owns its sidebar width for dragging, but the application owns the
        // settings file, so the width is reported back here to be remembered.
        m_performanceView = std::make_unique<ui::PerformanceView>(
            *m_coordinator,
            m_currentSettings,
            [this](double width) { m_currentSettings.performanceSidebarWidth = width; });
        // The page chosen in the settings, which _createPages resolved before the views existed.
        _selectPage(m_startupPageIndex);
    }

    void MainWindow::_selectPage(int32_t index)
    {
        // Selecting a navigation item during shell construction arrives before the
        // pages exist, so it is ignored; _createPages selects the default page once the
        // views are ready.
        if (m_contentHost == nullptr || m_processesView == nullptr || m_performanceView == nullptr)
        {
            return;
        }

        m_contentHost.Children().Clear();

        // The page header names the page being shown, matching the original's second bar.
        if (m_pageTitle != nullptr)
        {
            switch (index)
            {
                case 0:
                    m_pageTitle.Text(L"Processes");
                    m_currentPage = core::StartupPage::Processes;
                    break;
                case 1:
                    m_pageTitle.Text(L"Performance");
                    m_currentPage = core::StartupPage::Performance;
                    break;
                default:
                    m_pageTitle.Text(L"Details");
                    m_currentPage = core::StartupPage::Details;
                    break;
            }
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
                // The Details page arrives in M2. The placeholder states that plainly rather than
                // showing an empty grid.
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

    void MainWindow::_applySettings(core::Settings const& updated)
    {
        // The settings page edits its own copy, so the application's is brought into line here.
        m_currentSettings = updated;

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

        // The chart styles are pushed to the view so a colour change is visible without a restart.
        if (m_performanceView != nullptr)
        {
            m_performanceView->ApplySettings(updated);
        }

        if (m_detailsPage != nullptr)
        {
            // The details page plots a process's CPU share, so it takes the CPU style rather than one
            // of its own: a colour of its own would read as a different metric.
            core::ChartStyle const& cpu = updated.ChartStyleFor(0);
            m_detailsPage->SetAccentColor(winrt::Windows::UI::Color{0xFF, cpu.red, cpu.green, cpu.blue});
            m_detailsPage->SetLineWidth(cpu.ClampedLineWidth());
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
        m_navigationSplitter.Visibility(m_navigation.IsPaneOpen() ? winrt::Microsoft::UI::Xaml::Visibility::Visible
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
            item.Content(winrt::box_value(winrt::hstring{label}));
            FontIcon icon;
            icon.Glyph(glyph);
            item.Icon(icon);
            m_navigation.MenuItems().Append(item);
            return item;
        };

        // Segoe Fluent Icons glyphs.
        addItem(L"Processes", L"\xE9D9");
        addItem(L"Performance", L"\xE9D2");
        addItem(L"Details", L"\xE8FD");

        // Settings goes in the footer, which is where the original puts it: a separate list, so the
        // selection handler can tell it apart from the pages above.
        {
            NavigationViewItem settingsItem;
            settingsItem.Content(winrt::box_value(winrt::hstring{L"Settings"}));
            FontIcon settingsIcon;
            settingsIcon.Glyph(L"\xE713");
            settingsItem.Icon(settingsIcon);
            m_navigation.FooterMenuItems().Append(settingsItem);
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
                    _selectPage(SETTINGS_PAGE_INDEX);
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

        int32_t const startupIndex = (startupPage == core::StartupPage::Performance) ? 1
                                       : (startupPage == core::StartupPage::Details) ? 2
                                                                                     : 0;
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

        // --- Page header bar ----------------------------------------------------
        //
        // The second of the two bars the original has: the page name on the left, and the
        // task actions on the right. The buttons are placeholders -- they are present and
        // laid out, but do nothing yet -- because the original's actions belong to features
        // this milestone does not include (docs/ROADMAP.md).
        Grid pageHeader = Grid();
        pageHeader.Height(PAGE_HEADER_HEIGHT);
        pageHeader.Padding(ThicknessHelper::FromLengths(ui::metrics::PAGE_MARGIN, 0.0, 0.0, 0.0));
        pageHeader.Background(ui::controls::ThemedBrush(ui::theme::LAYER_BACKGROUND));

        // Column 0 takes the slack so the actions sit on the right at any width.
        pageHeader.ColumnDefinitions().Append(ui::controls::MakeStarColumn());
        pageHeader.ColumnDefinitions().Append(ui::controls::MakeAutoColumn());

        m_pageTitle = ui::controls::MakeHeading(L"Processes", 20.0);
        m_pageTitle.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(m_pageTitle, 0);
        pageHeader.Children().Append(m_pageTitle);

        StackPanel actions = ui::controls::MakeRow(4.0);
        actions.VerticalAlignment(VerticalAlignment::Center);
        actions.Margin(ThicknessHelper::FromLengths(0.0, 0.0, 12.0, 0.0));

        {
            // "Run new task" is a real button with an icon and a label, as in the original.
            winrt::Microsoft::UI::Xaml::Controls::Button runTask;
            runTask.Background(
                winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
            runTask.BorderThickness(ThicknessHelper::FromUniformLength(0.0));
            runTask.Padding(ThicknessHelper::FromLengths(10.0, 6.0, 10.0, 6.0));

            StackPanel runContent = ui::controls::MakeRow(8.0);
            winrt::Microsoft::UI::Xaml::Controls::FontIcon runIcon;
            runIcon.Glyph(L"\xE8A7"); // Segoe Fluent Icons: task view
            runIcon.FontSize(14.0);
            runContent.Children().Append(runIcon);
            runContent.Children().Append(ui::controls::MakeText(L"Run new task", 13.0));
            runTask.Content(runContent);

            // TODO: open the run-new-task dialog. The control is present so the bar matches
            //       the original; the dialog arrives with the process-control work in M2.
            runTask.Click([](winrt::Windows::Foundation::IInspectable const&,
                             winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {});

            actions.Children().Append(runTask);
        }

        {
            // The overflow menu, matching the original's "..." button.
            winrt::Microsoft::UI::Xaml::Controls::Button overflow;
            overflow.Background(
                winrt::Microsoft::UI::Xaml::Media::SolidColorBrush{winrt::Windows::UI::Colors::Transparent()});
            overflow.BorderThickness(ThicknessHelper::FromUniformLength(0.0));
            overflow.Padding(ThicknessHelper::FromLengths(8.0, 6.0, 8.0, 6.0));

            winrt::Microsoft::UI::Xaml::Controls::FontIcon moreIcon;
            moreIcon.Glyph(L"\xE712"); // Segoe Fluent Icons: More
            moreIcon.FontSize(14.0);
            overflow.Content(moreIcon);

            // TODO: show the settings and options menu.
            overflow.Click([](winrt::Windows::Foundation::IInspectable const&,
                              winrt::Microsoft::UI::Xaml::RoutedEventArgs const&) {});

            actions.Children().Append(overflow);
        }

        Grid::SetColumn(actions, 1);
        pageHeader.Children().Append(actions);

        // --- Assembly -----------------------------------------------------------
        //
        // Four rows: the app title bar, the page header, the page, then the bottom stack.
        // The page row is the only star, so it absorbs all remaining height. A row that is
        // meant to size to its content must be built with MakeAutoRow: a default-constructed
        // RowDefinition is 1* (Star) and would silently claim an equal share instead.
        Grid contentColumn = Grid();

        contentColumn.RowDefinitions().Append(ui::controls::MakeFixedRow(APP_TITLE_BAR_HEIGHT));
        contentColumn.RowDefinitions().Append(ui::controls::MakeFixedRow(PAGE_HEADER_HEIGHT));
        contentColumn.RowDefinitions().Append(ui::controls::MakeStarRow());
        contentColumn.RowDefinitions().Append(ui::controls::MakeAutoRow());

        // --- App title bar ------------------------------------------------------
        //
        // The first of the two bars: the application name, matching the original's
        // "Task Manager". The window buttons sit over its right-hand end, so nothing else
        // is drawn there.
        m_titleBarSpacer = Grid();
        m_titleBarSpacer.Padding(ThicknessHelper::FromLengths(ui::metrics::PAGE_MARGIN, 0.0, 0.0, 0.0));

        {
            StackPanel titleContent = ui::controls::MakeRow(10.0);
            titleContent.VerticalAlignment(VerticalAlignment::Center);

            // The application icon, as the original shows beside its title.
            winrt::Microsoft::UI::Xaml::Controls::FontIcon appIcon;
            appIcon.Glyph(L"\xE9D9"); // Segoe Fluent Icons: task view
            appIcon.FontSize(14.0);
            titleContent.Children().Append(appIcon);

            titleContent.Children().Append(ui::controls::MakeText(L"Task Manager", 12.0));

            m_titleBarSpacer.Children().Append(titleContent);
        }

        Grid::SetRow(m_titleBarSpacer, 0);
        contentColumn.Children().Append(m_titleBarSpacer);

        Grid::SetRow(pageHeader, 1);
        contentColumn.Children().Append(pageHeader);

        m_contentHost = Grid();
        Grid::SetRow(m_contentHost, 2);
        contentColumn.Children().Append(m_contentHost);

        // The permission bar and the status bar share the last row, the notice above
        // the status line, so neither shifts the page when it appears.
        Grid bottomStack = Grid();
        // Both size to their content; neither should absorb the page's height.
        bottomStack.RowDefinitions().Append(ui::controls::MakeAutoRow());
        bottomStack.RowDefinitions().Append(ui::controls::MakeAutoRow());
        Grid::SetRow(m_permissionBar, 0);
        bottomStack.Children().Append(m_permissionBar);
        Grid::SetRow(statusBar, 1);
        bottomStack.Children().Append(statusBar);

        Grid::SetRow(bottomStack, 3);
        contentColumn.Children().Append(bottomStack);

        m_navigation.Content(contentColumn);

        m_rootGrid = Grid();
        m_rootGrid.Background(ui::controls::ThemedBrush(ui::theme::PAGE_BACKGROUND));
        m_rootGrid.Children().Append(m_navigation);

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
        // position, so the boundary stays under the cursor wherever the drag starts.
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

                *dragStartWidth = m_navigation.OpenPaneLength();
                *dragStartX = args.GetCurrentPoint(element).Position().X;
                element.CapturePointer(args.Pointer());
            });

        m_navigationSplitter.PointerMoved(
            [this, dragStartWidth, dragStartX](
                winrt::Windows::Foundation::IInspectable const& sender,
                winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const& args) {
                auto const element = sender.try_as<winrt::Microsoft::UI::Xaml::UIElement>();
                if (element == nullptr || element.PointerCaptures().Size() == 0)
                {
                    return;
                }

                // Clamped between a width that still shows the labels and one that leaves the content
                // usable. A rail narrower than its longest label is unreadable, and one wider than
                // this crowds the page it exists to navigate.
                constexpr double MIN_RAIL = 180.0;
                constexpr double MAX_RAIL = 420.0;

                double const delta = args.GetCurrentPoint(element).Position().X - *dragStartX;
                double const width = std::clamp(*dragStartWidth + delta, MIN_RAIL, MAX_RAIL);

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

        // Extend into the title bar and nominate the strip as the drag region. The
        // navigation view is not used as the title bar because its own layout would
        // then be consulted for the button cut-out, which is what put the CPU readout
        // under the window buttons.
        ExtendsContentIntoTitleBar(true);
        SetTitleBar(m_titleBarSpacer);
        AppWindow().TitleBar().PreferredHeightOption(
            winrt::Microsoft::UI::Windowing::TitleBarHeightOption::Tall);

        // The remembered theme and the chart styles, applied once the root exists. Doing this only
        // from the settings page would mean a choice took effect only after visiting that page.
        _applySettings(m_currentSettings);

        _updateStatusBar();
    }

    void MainWindow::_startRefreshTimer()
    {
        auto const queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        m_refreshTimer = queue.CreateTimer();
        m_refreshTimer.Interval(UI_REFRESH_INTERVAL);
        m_refreshTimer.IsRepeating(true);
        m_refreshTimer.Tick([this](auto const&, auto const&) {
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
            if (m_activeDetailsPage != nullptr)
            {
                m_activeDetailsPage->Refresh();
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
