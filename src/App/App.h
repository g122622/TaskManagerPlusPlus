// Application object for TaskManagerPlusPlus.
#pragma once

#include "WinRT.h"

#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>

namespace tmpp
{
    /**
     * @brief WinUI 3 application object.
     *
     * The application composes its UI entirely in code. It still implements
     * IXamlMetadataProvider because WinUI resolves control templates and
     * theme resources through it at runtime.
     */
    // C++/WinRT runtime classes must not be final: the generated activation
    // factory derives from the implementation type.
    class App : public winrt::Microsoft::UI::Xaml::ApplicationT<App, winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider>
    {
    public:
        App();

        void OnLaunched(winrt::Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

        // IXamlMetadataProvider
        winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type);
        winrt::Microsoft::UI::Xaml::Markup::IXamlType GetXamlType(winrt::hstring const& fullName);
        winrt::com_array<winrt::Microsoft::UI::Xaml::Markup::XmlnsDefinition> GetXmlnsDefinitions();

    private:
        winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider m_metadataProvider;
        winrt::Microsoft::UI::Xaml::Window m_window{nullptr};
    };
}