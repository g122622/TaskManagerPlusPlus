#include "WinRT.h"

#include "App.h"
#include "MainWindow.h"
#include "StartupLog.h"

namespace tmpp
{
    App::App()
    {
        // NOTE: Application::Resources is not populated at construction time when
        // there is no App.xaml, so XamlControlsResources cannot be merged here.
        // It is merged in OnLaunched, once the application object is initialized.
        diag::LogStartup("App: ctor begin");

        UnhandledException([](winrt::Windows::Foundation::IInspectable const&,
                              winrt::Microsoft::UI::Xaml::UnhandledExceptionEventArgs const& args) {
            auto message = winrt::to_string(args.Message());
            diag::LogStartupError("UnhandledException", E_FAIL, message.c_str());
        });

        diag::LogStartup("App: ctor end");
    }

    void App::OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        diag::LogStartup("OnLaunched: entered");

        // Without App.xaml the WinUI control styles must be merged explicitly,
        // otherwise every built-in control falls back to an unstyled default.
        if (Resources().MergedDictionaries().Size() == 0)
        {
            Resources().MergedDictionaries().Append(
                winrt::Microsoft::UI::Xaml::Controls::XamlControlsResources());
            diag::LogStartup("OnLaunched: XamlControlsResources merged");
        }

        m_window = winrt::make<MainWindow>();
        diag::LogStartup("OnLaunched: MainWindow created");

        m_window.Activate();
        diag::LogStartup("OnLaunched: MainWindow activated");
    }

    winrt::Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type)
    {
        return m_metadataProvider.GetXamlType(type);
    }

    winrt::Microsoft::UI::Xaml::Markup::IXamlType App::GetXamlType(winrt::hstring const& fullName)
    {
        return m_metadataProvider.GetXamlType(fullName);
    }

    winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> App::GetXmlnsDefinitions()
    {
        return m_metadataProvider.GetXmlnsDefinitions();
    }
}
