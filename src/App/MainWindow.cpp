#include "WinRT.h"

#include "MainWindow.h"
#include "StartupLog.h"
#include "UiHelpers.h"

// Types used by this translation unit, made visible unqualified.
using winrt::Microsoft::UI::Xaml::Controls::FontIcon;
using winrt::Microsoft::UI::Xaml::Controls::Frame;
using winrt::Microsoft::UI::Xaml::Controls::Grid;
using winrt::Microsoft::UI::Xaml::Controls::NavigationView;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewBackButtonVisible;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewItem;
using winrt::Microsoft::UI::Xaml::Controls::NavigationViewPaneDisplayMode;

namespace tmpp
{
    MainWindow::MainWindow()
    {
        diag::LogStartup("MainWindow: ctor begin");

        Title(L"TaskManagerPlusPlus");
        diag::LogStartup("MainWindow: Title set");

        _buildContent();
        diag::LogStartup("MainWindow: content built");
    }

    void MainWindow::_buildContent()
    {
        // Step 1: a frame holding the placeholder page.
        m_contentFrame = Frame();
        diag::LogStartup("MainWindow: frame created");

        m_contentFrame.Content(MakePlaceholderPage());
        diag::LogStartup("MainWindow: placeholder page set");

        // Step 2: the navigation shell, mirroring the Windows 11 Task Manager
        // tabs that are in scope. The remaining tabs are deferred to the roadmap.
        m_navigation = NavigationView();
        diag::LogStartup("MainWindow: NavigationView created");

        m_navigation.IsSettingsVisible(true);
        m_navigation.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        m_navigation.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        m_navigation.Content(m_contentFrame);
        diag::LogStartup("MainWindow: NavigationView configured");

        auto addItem = [this](wchar_t const* label, wchar_t const* glyph) {
            NavigationViewItem item;
            item.Content(winrt::box_value(label));
            FontIcon icon;
            icon.Glyph(glyph);
            item.Icon(icon);
            m_navigation.MenuItems().Append(item);
            return item;
        };

        // Segoe Fluent Icons glyphs.
        auto processes = addItem(L"Processes", L"\xE9D9");
        addItem(L"Performance", L"\xE9D2");
        addItem(L"Details", L"\xE8FD");
        diag::LogStartup("MainWindow: menu items added");

        m_navigation.SelectedItem(processes);
        diag::LogStartup("MainWindow: selection set");

        m_rootGrid = Grid();
        m_rootGrid.Children().Append(m_navigation);
        diag::LogStartup("MainWindow: nav added to grid");

        Content(m_rootGrid);
        diag::LogStartup("MainWindow: window content set");
    }
}
