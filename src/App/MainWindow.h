// Main application window, composed entirely in code.
#pragma once

#include "WinRT.h"

namespace tmpp
{
    /**
     * @brief Main application window.
     *
     * The whole control tree is created in C++ rather than in XAML. This keeps
     * the build independent of the XAML compiler while still using the real
     * WinUI 3 control set.
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

        winrt::Microsoft::UI::Xaml::Controls::Grid m_rootGrid{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::NavigationView m_navigation{nullptr};
        winrt::Microsoft::UI::Xaml::Controls::Frame m_contentFrame{nullptr};
    };
}
